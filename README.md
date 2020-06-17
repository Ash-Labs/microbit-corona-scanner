# microbit-corona-scanner

The micro:bit finds BLE Beacons according to the Google/Apple COVID-19 Exposure Notification specification (https://www.blog.google/documents/70/Exposure_Notification_-_Bluetooth_Specification_v1.2.2.pdf)

## What does it do?
Currently there's just blinking LEDs if Exposure Notification Beacons are found.

## How to Build
This project uses yotta to build, not pxt.
This project uses the another SoftDevice(S130). That enables BLE Central feature.

Follow these steps to build the project.
** Don't forget copying NRF51822_S130.ld to NRF51822.ld ! **

```bash
# set target to use S130 SoftDevice.
yotta target bbc-microbit-classic-gcc-s130

# the linker uses `NRF51822.ld` file, then copy `NRF51822_S130.ld` to `NRF51822.ld`.
cp yotta_targets/bbc-microbit-classic-gcc-s130/ld/NRF51822_S130.ld yotta_targets/bbc-microbit-classic-gcc-s130/ld/NRF51822.ld

# build the project
yotta build

# transfer the hex file to micro:bit. (for example, macOS X)
cp build/bbc-microbit-classic-gcc-s130/source/microbit-ble-bridge-combined.hex /Volumes/MICROBIT/microbit-ble-bridge-combined.hex
```
