# webview tool
With this tool you can have a closer look at the devices lighting up the LEDs.

It requires the **websocketd** installed: [https://github.com/joewalnes/websocketd](https://github.com/joewalnes/websocketd)

**Linux advice:** Send your **ModemManager** to hell if you haven't done so already. It will interfere with all /dev/ttyACM devices.

## Usage
1) ensure the latest firmware version is installed on your micro:bit or Calliope Mini (at least v0.6.1 required)
2) ensure websocketd is installed on your host
3) connect your micro:bit or Calliope Mini via USB
4) launch the script: **./websocket-bridge.sh /dev/ttyACM0** (replace /dev/ttyACM0 with the correct serial device)
5) open [web/index.html](web/index.html) in your browser
6) enable USB output and ALLBLE mode with long clicks on buttons **A** and **B**
7) hover your mouse above the LEDs, click to select
