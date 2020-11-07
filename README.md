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
fan1:        1200 RPM
fan2:        1426 RPM
fan3:         918 RPM
fan4:         930 RPM
fan5:        1371 RPM
fan7:         967 RPM
CPU:          +49.0°C  
System:       +34.0°C  
VRM MOS:      +30.0°C  
PCH:          +40.0°C  
CPU Socket:   +33.0°C  
PCIe x1:      +32.0°C  
M2_1:          +0.0°C  
```

## Tested

This module was tested on Ubuntu 20.04 with [Linux Kernel 5.8.18-050818-generic](https://kernel.ubuntu.com/~kernel-ppa/mainline/v5.8.18/) on motherboard [MAG-B550-TOMAHAWK](https://www.msi.com//Motherboard/MAG-B550-TOMAHAWK) running an [AMD 3900X](https://www.amd.com/en/products/cpu/amd-ryzen-9-3900x)

## TODO

**1. Fan speed control**

- Changing fan speed and restore to default value not tested.
- Becareful probably fan speed will down to 0 RPM
