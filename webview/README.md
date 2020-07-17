# webview tool
With this tool you can have a closer look at the devices lighting up the LEDs.<br/>
It requires the **websocketd** installed: [https://github.com/joewalnes/websocketd](https://github.com/joewalnes/websocketd)<br/>

## Usage
1) ensure websocketd is installed
2) connect your micro:bit or Calliope Mini via USB
3) launch the script: **./websocket-bridge.sh /dev/ttyACM0** (replace /dev/ttyACM0 with the correct serial device)
4) open [web/index.html](web/index.html) in your browser
5) enable USB output and ALLBLE mode with long clicks on buttons **A** and **B**
6) hover your mouse above the LEDs, click to select
