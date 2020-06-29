# microbit-corona-scanner

The micro:bit finds BLE Beacons according to the Google/Apple COVID-19 Exposure Notification specification (https://www.blog.google/documents/70/Exposure_Notification_-_Bluetooth_Specification_v1.2.2.pdf)

## Video
[![usage video](https://img.youtube.com/vi/39K_UgLI7oA/0.jpg)](https://www.youtube.com/watch?v=39K_UgLI7oA)

## What does it do?
LEDs indicate received Exposure Notification beacons. There's one LED per Rolling Proximity Identifier (RPI) so up to 25 active RPIs with all 25 LEDs.

The number of RPIs seen is output every 10 seconds via the USB serial port.

Press *B* to change visualisation mode:
 * 0: persistence with fadeout from RSSI				[DEFAULT]
 * 1: blink with RSSI brightness
 * 2: persistence at full brightness
 * 3: blink at full brightness

Press *A* to toggle sound on/off. (Beacons from the device with the strongest signal do not trigger a click.)

Press *A* for 3 seconds to see all received Exposure Notifications via the USB serial port (format is RPI, AEM, RSSI). Press again for 3 seconds to disable.

## How to Build
This project uses yotta to build, not pxt.
This project uses the another SoftDevice(S130). That enables BLE Central feature.

Follow these steps to build the project.
** Don't forget copying NRF51822_S130.ld to NRF51822.ld ! **

```bash
# set target to use S130 SoftDevice.
yotta target bbc-microbit-classic-gcc-s130

# the linker uses `NRF51822.ld` file, then copy `NRF51822_S130.ld` to `NRF51822.ld`.
cp NRF51822_S130.ld yotta_targets/bbc-microbit-classic-gcc-s130/ld/NRF51822.ld

# build the project
yotta build

# transfer the hex file to micro:bit. (for example, macOS X)
cp build/bbc-microbit-classic-gcc-s130/source/corona-scanner-combined.hex /Volumes/MICROBIT/
```

## Building on Windows
get https://launchpad.net/gcc-arm-embedded/+download for arm gcc  
get https://github.com/ninja-build/ninja for building with cmake  
get https://sourceforge.net/projects/srecord/ for linking   

