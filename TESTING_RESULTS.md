# nct6687d MSI X870 SYS Fan Control - Testing Results

## Hardware
- **Motherboard**: MSI MAG X870E TOMAHAWK WIFI
- **Chip**: Nuvoton NCT6687D (EC firmware version 0.0 build 11/13/24)
- **Kernel**: Linux 6.17.4-arch2-1
- **Driver**: nct6687d with MSI X870 SYS fan control patch

## Problem
System fans (SYS_FAN1/2/3) on MSI X870 boards were not controllable via Linux. Only CPU_FAN and PUMP_FAN responded to PWM changes.

## Root Cause
The nct6687d driver used generic PWM control registers (0xA28+x) for all fans. MSI's X870 boards use different control registers for system fans, discovered through reverse engineering by the LibreHardwareMonitor project.

## Solution
Added per-fan PWM write control registers to `nct6687_fan_config` struct:
- CPU/PUMP fans: 0xA28, 0xA29 (unchanged)
- SYS fans: 0xC70, 0xC58, 0xC40, 0xC28, 0xC10, 0xBF8

## Test Results

### Before Patch
- SYS_FAN1 (rear exhaust): 0 RPM, no control
- SYS_FAN2 (front lower): 554 RPM, no control
- SYS_FAN3 (front upper): 560 RPM, no control

### After Patch
All fans fully controllable:
- **SYS_FAN1**: 0 → 1212 RPM (immediate response to PWM changes)
- **SYS_FAN2**: 554 → 1060 RPM (immediate response)
- **SYS_FAN3**: 560 → 1055 RPM (immediate response)

### CoolerControl Integration
- Successfully applies fan curves/fixed speeds
- Fans audibly respond to setting changes
- "MANUAL CONTROL" mode working correctly

## Commits
- `8ec1ea7` - Add per-fan PWM control registers for MSI X870 boards
- `8b0782c` - Fix MSI X870 SYS fan control register addresses

## Credits
Register addresses reverse-engineered from:
- LibreHardwareMonitor project: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor
- Specifically Nct677X.cs NCT6687DR implementation
- Thanks to @Alcolawl and contributors

## Status
✅ **WORKING** - Ready for upstream PR to https://github.com/Fred78290/nct6687d
