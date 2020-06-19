#include "MicroBitCustomConfig.h"
#include "MicroBit.h"
#include "ble/DiscoveredCharacteristic.h"
#include "ble/DiscoveredService.h"

#if YOTTA_CFG_MICROBIT_S130 != 1
#error This code *only* works with the Nordic S130 softdevice
#endif

#if CONFIG_ENABLED(MICROBIT_DBG)
#error use of the serial port by MICROBIT_DBG clashes with our use of the serial port - not uspported
#endif

extern "C" {
#include "device_manager.h"
uint32_t btle_set_gatt_table_size(uint32_t size);
}

MicroBit uBit;

static uint8_t nibble2hex(uint8_t n) {
	return (n + ((n < 10) ? ('0') : ('a' - 10)));
}

static char *tohex(char *dst, const uint8_t *src, uint32_t n) {
	for(;n--;src++,dst++) {
		uint8_t v = *src;
		*dst = nibble2hex(v>>4);
		dst++;
		*dst = nibble2hex(v&0xf);
	}
	return dst;
}

void advertisementCallback(const Gap::AdvertisementCallbackParams_t *params) {
    const uint8_t len = params->advertisingDataLen;
	const uint8_t *rxd = params->advertisingData;
	const uint8_t rssi = params->rssi;
	char buf[250], *p=buf;
	static int c='.';
	
	p+=sprintf(buf, "%3d %3d ", rssi, len);
	p=tohex(p, rxd, MIN(len, 100));
	p[0]='\r';
	p[1]='\n';
	p[2]=0;
	uBit.serial.send(buf, ASYNC);
	uBit.display.printCharAsync(c);
	c^=('.'^',');
}

int main() {
	
	uBit.serial.setTxBufferSize(250);
		
	btle_set_gatt_table_size(BLE_GATTS_ATTR_TAB_SIZE_MIN);
	
    uBit.ble = new BLEDevice();
    uBit.ble->init();

    uBit.ble->gap().setScanParams(500, 400);
    uBit.ble->gap().startScan(advertisementCallback);

    while (true) {
        uBit.ble->waitForEvent();
    }
    return 0;
}
