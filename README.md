![https://valid.x86.fr/vsb4yv](https://valid.x86.fr/cache/banner/vsb4yv-6.png)
![https://valid.x86.fr/20aiek](https://valid.x86.fr/cache/banner/20aiek-6.png)
# README

## NCT6687D Kernel module

This kernel module permit to recognize the chipset Nuvoton NCT6687-R in lm-sensors package.
This sensor is present on some B550 motherboard such as MSI or ASUS.

The implementation is minimalist and was done by reverse coding of Windows 10 source code from [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)

## Installation

To install this module, you need a buid environment. Exemple for Ubuntu

```shell
apt-get install build-essential linux-headers-`uname -r`
```

Clone this repository and go to source directory, just run make install. During install, you could be asked for your password because some commands are sudoed.

```shell
make install
```

## Sensors

By running the command sensors, you got this output

```
nct6687-isa-0a20
Adapter: ISA adapter
+12V:           12.17 V  (min = +12.17 V, max = +12.19 V)
+5V:             5.14 V  (min =  +5.14 V, max =  +5.14 V)
+3.3V:           3.38 V  (min =  +3.38 V, max =  +3.38 V)
CPU Soc:         1.11 V  (min =  +1.11 V, max =  +1.11 V)
CPU Vcore:       1.05 V  (min =  +0.97 V, max =  +1.05 V)
CPU 1P8:         1.84 V  (min =  +1.84 V, max =  +1.84 V)
CPU VDDP:        0.00 V  (min =  +0.00 V, max =  +0.00 V)
DRAM:            1.34 V  (min =  +1.34 V, max =  +1.35 V)
Chipset:       890.00 mV (min =  +0.89 V, max =  +0.89 V)
CPU Fan:       1192 RPM  (min = 1192 RPM, max = 1202 RPM)
Pump Fan:      1538 RPM  (min = 1526 RPM, max = 1538 RPM)
System Fan #1:  922 RPM  (min =  920 RPM, max =  922 RPM)
System Fan #2:  953 RPM  (min =  953 RPM, max =  953 RPM)
System Fan #3: 1393 RPM  (min = 1393 RPM, max = 1393 RPM)
System Fan #4:    0 RPM  (min =    0 RPM, max =    0 RPM)
System Fan #5: 1007 RPM  (min = 1007 RPM, max = 1007 RPM)
System Fan #6:    0 RPM  (min =    0 RPM, max =    0 RPM)
CPU:            +59.0°C  (low  = +52.0°C, high = +59.0°C)
System:         +34.0°C  (low  = +34.0°C, high = +34.0°C)
VRM MOS:        +31.0°C  (low  = +31.0°C, high = +31.0°C)
PCH:            +40.0°C  (low  = +40.0°C, high = +40.0°C)
CPU Socket:     +33.0°C  (low  = +33.0°C, high = +33.0°C)
PCIe x1:        +32.0°C  (low  = +32.0°C, high = +32.0°C)
M2_1:            +0.0°C  (low  =  +0.0°C, high =  +0.0°C)
```

## Load(prob) Sensors on boot

To make it loaded after system boots

Just add nct6687 into /etc/modules

`sudo sh -c 'echo "nct6687" >> /etc/modules'`

## Gnome sensors extensions

![Fan](./images/fan.png) ![Voltage](./images/voltage.png)
## Tested

This module was tested on Ubuntu 20.04 with [Linux Kernel 5.8.18-050818-generic](https://kernel.ubuntu.com/~kernel-ppa/mainline/v5.8.18/) on motherboard [MAG-B550-TOMAHAWK](https://www.msi.com//Motherboard/MAG-B550-TOMAHAWK) running an [AMD 3900X](https://www.amd.com/en/products/cpu/amd-ryzen-9-3900x)

## TODO

**1. Fan speed control**

- Changing fan speed and restore to default value not tested.
- Becareful probably fan speed could down to 0 RPM
