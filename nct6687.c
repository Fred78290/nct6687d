// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * nct6687 - Driver for the hardware monitoring functionality of
 *	       Nuvoton NCT6687 Super-I/O chips
 *
 * Copyright (C) 2020  Frederic Boltz <frederic.boltz@gmail.com>
 *
 * Derived from nct6683 driver
 * Copyright (C) 2013  Guenter Roeck <linux@roeck-us.net>
 * 
 * Inspired of LibreHardwareMonitor
 * https://github.com/LibreHardwareMonitor/LibreHardwareMonitor
 * 
 * Supports the following chips:
 *
 * Chip        #vin    #fan    #pwm    #temp  chip   IDs     man ID
 * nct6687    14      8       8       7      0xd592 0xc1    0x5ca3
 *
 * Notes:
 *	(1) Total number of vin and temp inputs is 32.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

enum kinds
{
	nct6687
};

static bool force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Set to one to enable support for unknown vendors");

static const char *const nct6687_device_names[] = {
	"nct6687",
};

static const char *const nct6687_chip_names[] = {
	"NCT6687D",
};

#define DRVNAME "nct6687"

/*
 * Super-I/O constants and functions
 */

#define NCT6687_LD_ACPI 0x0a
#define NCT6687_LD_HWM 0x0b
#define NCT6687_LD_VID 0x0d

#define SIO_REG_LDSEL 0x07		 /* Logical device select */
#define SIO_REG_DEVID 0x20		 /* Device ID (2 bytes) */
#define SIO_REG_DEVREVISION 0x21 /* Device ID (2 bytes) */
#define SIO_REG_ENABLE 0x30		 /* Logical device enable */
#define SIO_REG_ADDR 0x60		 /* Logical device address (2 bytes) */

#define SIO_NCT6687_ID 0xd592

static inline void superio_outb(int ioreg, int reg, int val)
{
	outb(reg, ioreg);
	outb(val, ioreg + 1);
}

static inline int superio_inb(int ioreg, int reg)
{
	outb(reg, ioreg);

	return inb(ioreg + 1);
}

static inline void superio_select(int ioreg, int ld)
{
	outb(SIO_REG_LDSEL, ioreg);
	outb(ld, ioreg + 1);
}

static inline int superio_enter(int ioreg)
{
	/*
	 * Try to reserve <ioreg> and <ioreg + 1> for exclusive access.
	 */
	if (!request_muxed_region(ioreg, 2, DRVNAME))
		return -EBUSY;

	outb(0x87, ioreg);
	outb(0x87, ioreg);

	return 0;
}

static inline void superio_exit(int ioreg)
{
	outb(0xaa, ioreg);
	outb(0x02, ioreg);
	outb(0x02, ioreg + 1);

	release_region(ioreg, 2);
}

/*
 * ISA constants
 */

#define IOREGION_OFFSET 0 /* Use EC port 1 */
#define IOREGION_LENGTH 4

/* Common and NCT6687 specific data */

#define NCT6687_NUM_REG_VOLTAGE 14
#define NCT6687_NUM_REG_TEMP 7
#define NCT6687_NUM_REG_FAN 8
#define NCT6687_NUM_REG_PWM 8

#define NCT6687_REG_TEMP(x) (0x100 + (x)*2)
#define NCT6687_REG_VOLTAGE(x) (0x120 + (x)*2)
#define NCT6687_REG_FAN_RPM(x) (0x140 + (x)*2)
#define NCT6687_REG_PWM(x) (0x160 + (x))
#define NCT6687_REG_PWM_WRITE(x) (0xa28 + (x))

#define NCT6687_HWM_CFG 0x180

#define NCT6687_REG_MON_CFG(x) (0x1a0 + (x))
#define NCT6687_REG_FANIN_CFG(x) (0xA00 + (x))
#define NCT6687_REG_FANOUT_CFG(x) (0x1d0 + (x))

#define NCT6687_REG_TEMP_HYST(x) (0x330 + (x))	/* 8 bit */
#define NCT6687_REG_TEMP_MAX(x) (0x350 + (x))	/* 8 bit */
#define NCT6687_REG_MON_HIGH(x) (0x370 + (x)*2) /* 8 bit */
#define NCT6687_REG_MON_LOW(x) (0x371 + (x)*2)	/* 8 bit */

#define NCT6687_REG_FAN_MIN(x) (0x3b8 + (x)*2) /* 16 bit */

#define NCT6687_REG_FAN_CTRL_MODE(x) 0xA00
#define NCT6687_REG_FAN_PWM_COMMAND(x) 0xA01
#define NCT6687_FAN_CFG_REQ 0x80
#define NCT6687_FAN_CFG_DONE 0x40

#define NCT6687_REG_BUILD_YEAR 0x604
#define NCT6687_REG_BUILD_MONTH 0x605
#define NCT6687_REG_BUILD_DAY 0x606
#define NCT6687_REG_SERIAL 0x607
#define NCT6687_REG_VERSION_HI 0x608
#define NCT6687_REG_VERSION_LO 0x609

#define NCT6687_REG_CR_CASEOPEN 0xe8
#define NCT6687_CR_CASEOPEN_MASK (1 << 7)

#define NCT6687_REG_CR_BEEP 0xe0
#define NCT6687_CR_BEEP_MASK (1 << 6)

#define EC_SPACE_PAGE_REGISTER_OFFSET 0x04
#define EC_SPACE_INDEX_REGISTER_OFFSET 0x05
#define EC_SPACE_DATA_REGISTER_OFFSET 0x06
#define EC_SPACE_PAGE_SELECT 0xFF

static const char *const nct6687_voltage_label[] = {
	"+12V",
	"+5V",
	"Vcore",
	"Voltage #1",
	"DIMM",
	"CPU I/O",
	"CPU SA",
	"Voltage #2",
	"AVCC3",
	"VTT",
	"VRef",
	"VSB",
	"AVSB",
	"VBat",
	NULL,
};

static const char *const nct6687_temp_label[] = {
	"CPU",
	"System",
	"VRM MOS",
	"PCH",
	"CPU Socket",
	"PCIe x1",
	"M2_1",
	NULL,
};

static const char *const nct6687_fan_label[] = {
	"CPU Fan",
	"Pump Fan",
	"System Fan #1",
	"System Fan #2",
	"System Fan #3",
	"System Fan #4",
	"System Fan #5",
	"System Fan #6",
	NULL,
};

/* ------------------------------------------------------- */

struct nct6687_data
{
	int addr;	/* IO base of EC space */
	int sioreg; /* SIO register */
	enum kinds kind;

	struct device *hwmon_dev;
	const struct attribute_group *groups[6];

	struct mutex update_lock;	/* used to protect sensor updates */
	bool valid;					/* true if following fields are valid */
	unsigned long last_updated; /* In jiffies */

	/* Voltage values */
	s16 voltage[NCT6687_NUM_REG_VOLTAGE];

	/* Temperature values */
	s32 temperature[NCT6687_NUM_REG_TEMP];

	/* Fan attribute values */
	u16 have_fan; /* some fan inputs can be disabled */
	u16 rpm[NCT6687_NUM_REG_FAN];
	u8 _initialFanControlMode[NCT6687_NUM_REG_FAN];
	u8 _initialFanPwmCommand[NCT6687_NUM_REG_FAN];
	bool _restoreDefaultFanControlRequired[NCT6687_NUM_REG_FAN];
	u16 fan_min[NCT6687_NUM_REG_FAN];

	u8 have_pwm;
	u8 pwm[NCT6687_NUM_REG_PWM];

	/* Remember extra register values over suspend/resume */
	u8 hwm_cfg;
};

struct nct6687_sio_data
{
	int sioreg;
	enum kinds kind;
};

struct sensor_device_template
{
	struct device_attribute dev_attr;
	union
	{
		struct
		{
			u8 nr;
			u8 index;
		} s;
		int index;
	} u;
	bool s2; /* true if both index and nr are used */
};

struct sensor_device_attr_u
{
	union
	{
		struct sensor_device_attribute a1;
		struct sensor_device_attribute_2 a2;
	} u;
	char name[32];
};

#define __TEMPLATE_ATTR(_template, _mode, _show, _store) \
	{                                                    \
		.attr = {.name = _template, .mode = _mode},      \
		.show = _show,                                   \
		.store = _store,                                 \
	}

#define SENSOR_DEVICE_TEMPLATE(_template, _mode, _show, _store, _index) \
	{                                                                   \
		.dev_attr = __TEMPLATE_ATTR(_template, _mode, _show, _store),   \
		.u.index = _index,                                              \
		.s2 = false                                                     \
	}

#define SENSOR_DEVICE_TEMPLATE_2(_template, _mode, _show, _store,     \
								 _nr, _index)                         \
	{                                                                 \
		.dev_attr = __TEMPLATE_ATTR(_template, _mode, _show, _store), \
		.u.s.index = _index,                                          \
		.u.s.nr = _nr,                                                \
		.s2 = true                                                    \
	}

#define SENSOR_TEMPLATE(_name, _template, _mode, _show, _store, _index)                                                        \
	static struct sensor_device_template sensor_dev_template_##_name = SENSOR_DEVICE_TEMPLATE(_template, _mode, _show, _store, \
																							  _index)

#define SENSOR_TEMPLATE_2(_name, _template, _mode, _show, _store,                                                                \
						  _nr, _index)                                                                                           \
	static struct sensor_device_template sensor_dev_template_##_name = SENSOR_DEVICE_TEMPLATE_2(_template, _mode, _show, _store, \
																								_nr, _index)

struct sensor_template_group
{
	struct sensor_device_template **templates;
	umode_t (*is_visible)(struct kobject *, struct attribute *, int);
	int base;
};

static void nct6687_save_fan_control(struct nct6687_data *data, int index);

static struct attribute_group *nct6687_create_attr_group(struct device *dev, const struct sensor_template_group *tg, int repeat)
{
	struct sensor_device_attribute_2 *a2;
	struct sensor_device_attribute *a;
	struct sensor_device_template **t;
	struct sensor_device_attr_u *su;
	struct attribute_group *group;
	struct attribute **attrs;
	int i, j, count;

	if (repeat <= 0)
		return ERR_PTR(-EINVAL);

	t = tg->templates;
	for (count = 0; *t; t++, count++)
		;

	if (count == 0)
		return ERR_PTR(-EINVAL);

	group = devm_kzalloc(dev, sizeof(*group), GFP_KERNEL);
	if (group == NULL)
		return ERR_PTR(-ENOMEM);

	attrs = devm_kcalloc(dev, repeat * count + 1, sizeof(*attrs), GFP_KERNEL);
	if (attrs == NULL)
		return ERR_PTR(-ENOMEM);

	su = devm_kzalloc(dev, array3_size(repeat, count, sizeof(*su)), GFP_KERNEL);
	if (su == NULL)
		return ERR_PTR(-ENOMEM);

	group->attrs = attrs;
	group->is_visible = tg->is_visible;

	for (i = 0; i < repeat; i++)
	{
		t = tg->templates;

		for (j = 0; *t != NULL; j++)
		{
			snprintf(su->name, sizeof(su->name), (*t)->dev_attr.attr.name, tg->base + i);

			if ((*t)->s2)
			{
				a2 = &su->u.a2;
				sysfs_attr_init(&a2->dev_attr.attr);
				a2->dev_attr.attr.name = su->name;
				a2->nr = (*t)->u.s.nr + i;
				a2->index = (*t)->u.s.index;
				a2->dev_attr.attr.mode = (*t)->dev_attr.attr.mode;
				a2->dev_attr.show = (*t)->dev_attr.show;
				a2->dev_attr.store = (*t)->dev_attr.store;
				*attrs = &a2->dev_attr.attr;
			}
			else
			{
				a = &su->u.a1;
				sysfs_attr_init(&a->dev_attr.attr);
				a->dev_attr.attr.name = su->name;
				a->index = (*t)->u.index + i;
				a->dev_attr.attr.mode = (*t)->dev_attr.attr.mode;
				a->dev_attr.show = (*t)->dev_attr.show;
				a->dev_attr.store = (*t)->dev_attr.store;
				*attrs = &a->dev_attr.attr;
			}
			attrs++;
			su++;
			t++;
		}
	}

	return group;
}

static u16 nct6687_read(struct nct6687_data *data, u16 address)
{
	u8 page = (u8)(address >> 8);
	u8 index = (u8)(address & 0xFF);
	int res;

	outb_p(EC_SPACE_PAGE_SELECT, data->addr + EC_SPACE_PAGE_REGISTER_OFFSET);
	outb_p(page, data->addr + EC_SPACE_PAGE_REGISTER_OFFSET);
	outb_p(index, data->addr + EC_SPACE_INDEX_REGISTER_OFFSET);

	res = inb_p(data->addr + EC_SPACE_DATA_REGISTER_OFFSET);

	return res;
}

static u16 nct6687_read16(struct nct6687_data *data, u16 reg)
{
	return (nct6687_read(data, reg) << 8) | nct6687_read(data, reg + 1);
}

static void nct6687_write(struct nct6687_data *data, u16 address, u16 value)
{
	u8 page = (u8)(address >> 8);
	u8 index = (u8)(address & 0xFF);

	outb_p(EC_SPACE_PAGE_SELECT, data->addr + EC_SPACE_PAGE_REGISTER_OFFSET);
	outb_p(page, data->addr + EC_SPACE_PAGE_REGISTER_OFFSET);
	outb_p(index, data->addr + EC_SPACE_INDEX_REGISTER_OFFSET);
	outb_p(value, data->addr + EC_SPACE_DATA_REGISTER_OFFSET);
}

static void nct6687_update_pwm(struct device *dev)
{
	struct nct6687_data *data = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < NCT6687_NUM_REG_PWM; i++)
	{
		if (!(data->have_pwm & (1 << i)))
			continue;
		data->pwm[i] = nct6687_read(data, NCT6687_REG_PWM(i));
	}
}

static struct nct6687_data *nct6687_update_device(struct device *dev)
{
	struct nct6687_data *data = dev_get_drvdata(dev);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid)
	{
		/* Measured voltages and limits */
		for (i = 0; i < NCT6687_NUM_REG_VOLTAGE; i++)
		{
			s16 high = nct6687_read(data, NCT6687_REG_TEMP(i)) * 16;
			s16 low = ((u16)nct6687_read(data, NCT6687_REG_TEMP(i) + 1)) >> 4;
			s16 value = low + high;

			switch (i)
			{
			case 0:
				value *= 12;
				break;
			case 1:
				value *= 5;
				break;
			case 4:
				value *= 2;
				break;

			default:
				break;
			}
			data->voltage[i] = value;
		}

		/* Measured temperatures and limits */
		for (i = 0; i < NCT6687_NUM_REG_TEMP; i++)
		{
			s32 value = (char)nct6687_read(data, NCT6687_REG_TEMP(i));
			s32 half = (nct6687_read(data, NCT6687_REG_TEMP(i) + 1) >> 7) & 0x1;

			data->temperature[i] = (value * 1000) + (5 * half);
		}

		/* Measured fan speeds and limits */
		for (i = 0; i < ARRAY_SIZE(data->rpm); i++)
		{
			if (!(data->have_fan & (1 << i)))
				continue;

			data->rpm[i] = nct6687_read16(data, NCT6687_REG_FAN_RPM(i));
			data->fan_min[i] = nct6687_read16(data, NCT6687_REG_FAN_MIN(i));
		}

		nct6687_update_pwm(dev);

		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs callback functions
 */
static ssize_t show_voltage_label(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	return sprintf(buf, "%s\n", nct6687_voltage_label[sattr->index]);
}

static ssize_t show_voltage(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct6687_data *data = nct6687_update_device(dev);
	int index = sattr->index;

	return sprintf(buf, "%d\n", data->voltage[index]);
}

static umode_t nct6687_voltage_is_visible(struct kobject *kobj, struct attribute *attr, int index)
{
	return attr->mode;
}

SENSOR_TEMPLATE(in_label, "voltage%d_label", S_IRUGO, show_voltage_label, NULL, 0);
SENSOR_TEMPLATE_2(in_input, "voltage%d_input", S_IRUGO, show_voltage, NULL, 0, 0);
SENSOR_TEMPLATE_2(in_min, "voltage%d_min", S_IRUGO, show_voltage, NULL, 0, 1);
SENSOR_TEMPLATE_2(in_max, "voltage%d_max", S_IRUGO, show_voltage, NULL, 0, 2);

static struct sensor_device_template *nct6687_attributes_in_template[] = {
	&sensor_dev_template_in_label,
	&sensor_dev_template_in_input,
	&sensor_dev_template_in_min,
	&sensor_dev_template_in_max,
	NULL,
};

static const struct sensor_template_group nct6687_in_template_group = {
	.templates = nct6687_attributes_in_template,
	.is_visible = nct6687_voltage_is_visible,
};

static ssize_t show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6687_data *data = nct6687_update_device(dev);

	return sprintf(buf, "%d\n", data->rpm[sattr->index]);
}

static ssize_t show_fan_min(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6687_data *data = nct6687_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;

	return sprintf(buf, "%d\n", data->fan_min[nr]);
}

static umode_t nct6687_fan_is_visible(struct kobject *kobj, struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nct6687_data *data = dev_get_drvdata(dev);
	int fan = index; /* fan index */

	if (!(data->have_fan & (1 << fan)))
		return 0;

	return attr->mode;
}

SENSOR_TEMPLATE(fan_input, "fan%d_input", S_IRUGO, show_fan, NULL, 0);
//SENSOR_TEMPLATE(fan_min, "fan%d_min", S_IRUGO, show_fan_min, NULL, 0);

/*
 * nct6687_fan_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct sensor_device_template *nct6687_attributes_fan_template[] = {
	&sensor_dev_template_fan_input,
//	&sensor_dev_template_fan_min,
	NULL,
};

static const struct sensor_template_group nct6687_fan_template_group = {
	.templates = nct6687_attributes_fan_template,
	.is_visible = nct6687_fan_is_visible,
	.base = 1,
};

static ssize_t show_temperature_label(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	return sprintf(buf, "%s\n", nct6687_temp_label[sattr->index]);
}

static ssize_t show_temperature_value(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6687_data *data = nct6687_update_device(dev);
	int index = sattr->index;

	return sprintf(buf, "%d\n", data->temperature[index]);
}

static umode_t nct6687_temp_is_visible(struct kobject *kobj, struct attribute *attr, int index)
{
	return attr->mode;
}

SENSOR_TEMPLATE(temp_input, "temp%d_input", S_IRUGO, show_temperature_value, NULL, 0);
SENSOR_TEMPLATE(temp_label, "temp%d_label", S_IRUGO, show_temperature_label, NULL, 0);

/*
 * nct6687_temp_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct sensor_device_template *nct6687_attributes_temp_template[] = {
	&sensor_dev_template_temp_input,
	&sensor_dev_template_temp_label,
	NULL,
};

static const struct sensor_template_group nct6687_temp_template_group = {
	.templates = nct6687_attributes_temp_template,
	.is_visible = nct6687_temp_is_visible,
	.base = 1,
};

static ssize_t show_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6687_data *data = nct6687_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int index = sattr->index;

	return sprintf(buf, "%d\n", data->pwm[index]);
}

static ssize_t store_pwm(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct6687_data *data = dev_get_drvdata(dev);
	int index = sattr->index;
	unsigned long val;
	u16 mode;
	u8 bitMask;

	if (kstrtoul(buf, 10, &val) || val > 255 || index >= NCT6687_NUM_REG_FAN)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	nct6687_save_fan_control(data, index);

	mode = nct6687_read(data, NCT6687_REG_FAN_CTRL_MODE(index));
	bitMask = (u8)(0x01 << index);

	mode = (u8)(mode | bitMask);
	nct6687_write(data, NCT6687_REG_FAN_CTRL_MODE(index), mode);

	nct6687_write(data, NCT6687_REG_FAN_PWM_COMMAND(index), NCT6687_FAN_CFG_REQ);
	msleep(50);
	nct6687_write(data, NCT6687_REG_PWM_WRITE(index), val);
	nct6687_write(data, NCT6687_REG_FAN_PWM_COMMAND(index), NCT6687_FAN_CFG_DONE);
	msleep(50);

	mutex_unlock(&data->update_lock);

	return count;
}

SENSOR_TEMPLATE(pwm, "pwm%d", S_IRUGO, show_pwm, store_pwm, 0);

static void nct6687_save_fan_control(struct nct6687_data *data, int index)
{
	if (data->_restoreDefaultFanControlRequired[index] == false)
	{
		u16 reg = nct6687_read(data, NCT6687_REG_FAN_CTRL_MODE(index));
		u16 bitMask = 0x01 << index;
		u8 pwm = nct6687_read(data, NCT6687_REG_FAN_PWM_COMMAND(index));

		data->_initialFanControlMode[index] = (u8)(reg & bitMask);
		data->_initialFanPwmCommand[index] = pwm;

		data->_restoreDefaultFanControlRequired[index] = true;
	}
}

static void nct6687_restore_fan_control(struct nct6687_data *data, int index)
{
	if (data->_restoreDefaultFanControlRequired[index])
	{
		u8 mode = nct6687_read(data, NCT6687_REG_FAN_CTRL_MODE(index));
		mode = (u8)(mode & ~data->_initialFanControlMode[index]);

		nct6687_write(data, NCT6687_REG_FAN_CTRL_MODE(index), mode);

		nct6687_write(data, NCT6687_REG_FAN_PWM_COMMAND(index), NCT6687_FAN_CFG_REQ);
		msleep(50);
		nct6687_write(data, NCT6687_REG_PWM_WRITE(index), data->_initialFanPwmCommand[index]);
		nct6687_write(data, NCT6687_REG_FAN_PWM_COMMAND(index), NCT6687_FAN_CFG_DONE);
		msleep(50);

		data->_restoreDefaultFanControlRequired[index] = false;

		pr_debug("nct6687_restore_fan_control[%d], addr=%04X, ctrl=%04X, _initialFanPwmCommand=%d\n", index, NCT6687_REG_FAN_PWM_COMMAND(index), NCT6687_REG_PWM_WRITE(index), data->_initialFanPwmCommand[index]);
	}
}

static umode_t nct6687_pwm_is_visible(struct kobject *kobj, struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nct6687_data *data = dev_get_drvdata(dev);
	int pwm = index; /* pwm index */

	if (!(data->have_pwm & (1 << pwm)))
		return 0;

	/* Only update pwm values for Mitac boards */
	//if (data->customer_id == NCT6687_CUSTOMER_ID_MITAC)
	return attr->mode | S_IWUSR;

	//return attr->mode;
}

static struct sensor_device_template *nct6687_attributes_pwm_template[] = {
	&sensor_dev_template_pwm,
	NULL,
};

static const struct sensor_template_group nct6687_pwm_template_group = {
	.templates = nct6687_attributes_pwm_template,
	.is_visible = nct6687_pwm_is_visible,
	.base = 1,
};

/* Get the monitoring functions started */
static inline void nct6687_init_device(struct nct6687_data *data)
{
	u8 tmp;

	pr_debug("nct6687_init_device\n");

	/* Start hardware monitoring if needed */
	tmp = nct6687_read(data, NCT6687_HWM_CFG);
	if (!(tmp & 0x80))
	{
		pr_debug("nct6687_init_device: 0x%04x\n", tmp);
		nct6687_write(data, NCT6687_HWM_CFG, tmp | 0x80);
	}

	// enable SIO voltage
	nct6687_write(data, 0x1BB, 0x61);
	nct6687_write(data, 0x1BC, 0x62);
	nct6687_write(data, 0x1BD, 0x63);
	nct6687_write(data, 0x1BE, 0x64);
	nct6687_write(data, 0x1BF, 0x65);
}

/*
 * There are a total of 8 fan inputs.
 */
static void nct6687_setup_fans(struct nct6687_data *data)
{
	int i;

	for (i = 0; i < NCT6687_NUM_REG_FAN; i++)
	{
		u16 reg = nct6687_read(data, NCT6687_REG_FAN_CTRL_MODE(i));
		u16 bitMask = 0x01 << i;

		data->_initialFanControlMode[i] = (u8)(reg & bitMask);
		data->rpm[i] = nct6687_read16(data, NCT6687_REG_FAN_RPM(i));
		data->fan_min[i] = nct6687_read16(data, NCT6687_REG_FAN_MIN(i));
		data->_restoreDefaultFanControlRequired[i] = false;

		if (data->rpm[i])
			data->have_fan |= 1 << i;

		pr_debug("nct6687_setup_fans[%d], addr=%04X, ctrl=%04X, rpm=%d, fan min=%d, _initialFanControlMode=%d\n", i, NCT6687_REG_FAN_CTRL_MODE(i), reg, data->rpm[i], data->fan_min[i], data->_initialFanControlMode[i]);
	}
}

static void nct6687_setup_pwm(struct nct6687_data *data)
{
	int i;

	for (i = 0; i < NCT6687_NUM_REG_PWM; i++)
	{
		data->have_pwm |= 1 << i;
		data->_initialFanPwmCommand[i] = nct6687_read(data, NCT6687_REG_FAN_PWM_COMMAND(i));
		data->pwm[i] = nct6687_read(data, NCT6687_REG_PWM(i));

		pr_debug("nct6687_setup_pwm[%d]], addr=%04X, pwm=%d, _initialFanPwmCommand=%d\n", i, NCT6687_REG_FAN_PWM_COMMAND(i), data->pwm[i], data->_initialFanPwmCommand[i]);
	}
}

static void nct6687_setup_temperatures(struct nct6687_data *data)
{
	int i;

	for (i = 0; i < NCT6687_NUM_REG_TEMP; i++)
	{
		s32 value = (char)nct6687_read(data, NCT6687_REG_TEMP(i));
		s32 half = (nct6687_read(data, NCT6687_REG_TEMP(i) + 1) >> 7) & 0x1;

		data->temperature[i] = (value * 1000) + (5 * half);

		pr_debug("nct6687_setup_temperatures[%d]], addr=%04X, value=%d, half=%d, temperature=%d\n", i, NCT6687_REG_TEMP(i), value, half, data->temperature[i]);
	}
}

static void nct6687_setup_voltage(struct nct6687_data *data)
{
	int i;

	/* Measured voltages and limits */
	for (i = 0; i < NCT6687_NUM_REG_VOLTAGE; i++)
	{
		s16 high = nct6687_read(data, NCT6687_REG_TEMP(i)) * 16;
		s16 low = ((u16)nct6687_read(data, NCT6687_REG_TEMP(i) + 1)) >> 4;
		s16 value = low + high;

		switch (i)
		{
		case 0:
			value *= 12;
			break;
		case 1:
			value *= 5;
			break;
		case 4:
			value *= 2;
			break;

		default:
			break;
		}

		data->voltage[i] = value;

		pr_debug("nct6687_setup_voltage[%d], addr=0x%04x, voltage=%d\n", i, NCT6687_REG_TEMP(i), value);
	}
}

static int nct6687_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nct6687_data *data = dev_get_drvdata(dev);
	int i;

	mutex_lock(&data->update_lock);

	for (i = 0; i < NCT6687_NUM_REG_FAN; i++)
	{
		nct6687_restore_fan_control(data, i);
	}

	mutex_unlock(&data->update_lock);

	return 0;
}

static int nct6687_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nct6687_sio_data *sio_data = dev->platform_data;
	struct attribute_group *group;
	struct nct6687_data *data;
	struct device *hwmon_dev;
	struct resource *res;
	int groups = 0;
	char build[16];

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!devm_request_region(dev, res->start, IOREGION_LENGTH, DRVNAME))
		return -EBUSY;

	data = devm_kzalloc(dev, sizeof(struct nct6687_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->kind = sio_data->kind;
	data->sioreg = sio_data->sioreg;
	data->addr = res->start;

	pr_debug("nct6687_probe addr=0x%04X, sioreg=0x%04X\n", data->addr, data->sioreg);

	mutex_init(&data->update_lock);

	platform_set_drvdata(pdev, data);

	nct6687_init_device(data);
	nct6687_setup_fans(data);
	nct6687_setup_pwm(data);
	nct6687_setup_temperatures(data);
	nct6687_setup_voltage(data);

	/* Register sysfs hooks */

	if (data->have_pwm)
	{
		group = nct6687_create_attr_group(dev, &nct6687_pwm_template_group, fls(data->have_pwm));

		if (IS_ERR(group))
			return PTR_ERR(group);

		data->groups[groups++] = group;
	}

	group = nct6687_create_attr_group(dev, &nct6687_in_template_group, NCT6687_NUM_REG_VOLTAGE);

	if (IS_ERR(group))
		return PTR_ERR(group);

	data->groups[groups++] = group;

	if (data->have_fan)
	{
		group = nct6687_create_attr_group(dev, &nct6687_fan_template_group, fls(data->have_fan));

		if (IS_ERR(group))
			return PTR_ERR(group);

		data->groups[groups++] = group;
	}

	group = nct6687_create_attr_group(dev, &nct6687_temp_template_group, NCT6687_NUM_REG_TEMP);

	if (IS_ERR(group))
		return PTR_ERR(group);

	data->groups[groups++] = group;

	scnprintf(build, sizeof(build), "%02d/%02d/%02d", nct6687_read(data, NCT6687_REG_BUILD_MONTH), nct6687_read(data, NCT6687_REG_BUILD_DAY), nct6687_read(data, NCT6687_REG_BUILD_YEAR));

	dev_info(dev, "%s EC firmware version %d.%d build %s\n", nct6687_chip_names[data->kind], nct6687_read(data, NCT6687_REG_VERSION_HI), nct6687_read(data, NCT6687_REG_VERSION_LO), build);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, nct6687_device_names[data->kind], data, data->groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int nct6687_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct nct6687_data *data = nct6687_update_device(&pdev->dev);

	mutex_lock(&data->update_lock);
	data->hwm_cfg = nct6687_read(data, NCT6687_HWM_CFG);
	mutex_unlock(&data->update_lock);

	return 0;
}

static int nct6687_resume(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nct6687_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->update_lock);

	nct6687_write(data, NCT6687_HWM_CFG, data->hwm_cfg);

	/* Force re-reading all values */
	data->valid = false;
	mutex_unlock(&data->update_lock);

	return 0;
}

// static const struct dev_pm_ops nct6687_dev_pm_ops = {
// 	.suspend = nct6687_suspend,
// 	.resume = nct6687_resume,
// 	.freeze = nct6687_suspend,
// 	.restore = nct6687_resume,
// };

#define NCT6687_DEV_PM_OPS NULL

static struct platform_driver nct6687_driver = {
	.driver = {
		.name = DRVNAME,
		.pm = NCT6687_DEV_PM_OPS,
	},
	.probe = nct6687_probe,
	.remove = nct6687_remove,
	.suspend = nct6687_suspend,
	.resume = nct6687_resume,
};

static int __init nct6687_find(int sioaddr, struct nct6687_sio_data *sio_data)
{
	u16 address;
	u16 verify;
	u16 val;
	int err;

	err = superio_enter(sioaddr);
	if (err)
		return err;

	val = (superio_inb(sioaddr, SIO_REG_DEVID) << 8) | superio_inb(sioaddr, SIO_REG_DEVREVISION);

	if (val == SIO_NCT6687_ID)
	{
		sio_data->kind = nct6687;
	}
	else
	{
		if (val != 0xffff)
			pr_debug("unsupported chip ID: 0x%04x\n", val);
		goto fail;
	}

	/* We have a known chip, find the HWM I/O address */
	superio_select(sioaddr, NCT6687_LD_HWM);
	address = (superio_inb(sioaddr, SIO_REG_ADDR) << 8) | superio_inb(sioaddr, SIO_REG_ADDR + 1);
	ssleep(1);
	verify = (superio_inb(sioaddr, SIO_REG_ADDR) << 8) | superio_inb(sioaddr, SIO_REG_ADDR + 1);

	if (address == 0 || address != verify)
	{
		pr_err("EC base I/O port unconfigured\n");
		goto fail;
	}

	if ((address & 0x07) == 0x05)
		address &= 0xFFF8;

	if (address < 0x100 || (address & 0xF007) != 0)
	{
		pr_err("EC Invalid address: 0x%04X\n", address);
		goto fail;
	}

	/* Activate logical device if needed */
	val = superio_inb(sioaddr, SIO_REG_ENABLE);
	if (!(val & 0x01))
	{
		pr_warn("Forcibly enabling EC access. Data may be unusable.\n");
		superio_outb(sioaddr, SIO_REG_ENABLE, val | 0x01);
	}

	superio_exit(sioaddr);
	pr_info("Found %s or compatible chip at 0x%04x:0x%04x\n", nct6687_chip_names[sio_data->kind], sioaddr, address);
	sio_data->sioreg = sioaddr;

	return address;

fail:
	superio_exit(sioaddr);
	return -ENODEV;
}

/*
 * when Super-I/O functions move to a separate file, the Super-I/O
 * bus will manage the lifetime of the device and this module will only keep
 * track of the nct6687 driver. But since we use platform_device_alloc(), we
 * must keep track of the device
 */
static struct platform_device *pdev[2];

static int __init sensors_nct6687_init(void)
{
	struct nct6687_sio_data sio_data;
	int sioaddr[2] = {0x2e, 0x4e};
	struct resource res;
	bool found = false;
	int address;
	int i, err;

	err = platform_driver_register(&nct6687_driver);
	if (err)
		return err;

	/*
	 * initialize sio_data->kind and sio_data->sioreg.
	 *
	 * when Super-I/O functions move to a separate file, the Super-I/O
	 * driver will probe 0x2e and 0x4e and auto-detect the presence of a
	 * nct6687 hardware monitor, and call probe()
	 */
	for (i = 0; i < ARRAY_SIZE(pdev); i++)
	{
		address = nct6687_find(sioaddr[i], &sio_data);
		if (address <= 0)
			continue;

		found = true;

		pdev[i] = platform_device_alloc(DRVNAME, address);
		if (!pdev[i])
		{
			err = -ENOMEM;
			goto exit_device_unregister;
		}

		err = platform_device_add_data(pdev[i], &sio_data, sizeof(struct nct6687_sio_data));
		if (err)
			goto exit_device_put;

		memset(&res, 0, sizeof(res));

		res.name = DRVNAME;
		res.start = address + IOREGION_OFFSET;
		res.end = address + IOREGION_OFFSET + IOREGION_LENGTH - 1;
		res.flags = IORESOURCE_IO;

		err = acpi_check_resource_conflict(&res);
		if (err)
		{
			platform_device_put(pdev[i]);
			pdev[i] = NULL;
			continue;
		}

		err = platform_device_add_resources(pdev[i], &res, 1);
		if (err)
			goto exit_device_put;

		/* platform_device_add calls probe() */
		err = platform_device_add(pdev[i]);
		if (err)
			goto exit_device_put;
	}

	if (!found)
	{
		err = -ENODEV;
		goto exit_unregister;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev[i]);

exit_device_unregister:
	while (--i >= 0)
	{
		if (pdev[i])
			platform_device_unregister(pdev[i]);
	}

exit_unregister:
	platform_driver_unregister(&nct6687_driver);

	return err;
}

static void __exit sensors_nct6687_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pdev); i++)
	{
		if (pdev[i])
			platform_device_unregister(pdev[i]);
	}

	platform_driver_unregister(&nct6687_driver);
}

MODULE_AUTHOR("Frederic Boltz <frederic.boltz@gmail.com>");
MODULE_DESCRIPTION("Driver for NCT6687D");
MODULE_LICENSE("GPL");

module_init(sensors_nct6687_init);
module_exit(sensors_nct6687_exit);
