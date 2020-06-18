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

MicroBit uBit;

static unsigned long last_rx = 0;
static uint32_t rxcount = 0;

struct rpi_s {
	long last_seen;
	uint16_t rpi_start;
	uint8_t rssi;
	uint8_t rfu;
};

/* TODO: sorted list of last_seen -> rpi entry mapping for entry reuse */

static struct rpi_s rpi_list[25];

void timer(void) {
	static uint32_t last_rxcount = 0;
	
	if(rxcount == last_rxcount)
		return;
	else {
		unsigned long now = uBit.systemTime();
		if(now - last_rx > 50) {
			last_rxcount = rxcount;
			uBit.display.printChar(' ');
		}
	}
}

/*
#define MIN(a,b) (((a)<=(b))?(a):(b))

uint8_t last_rssi = 255;
uint8_t min_rssi = 255;

void rssi_test(const uint8_t rssi) {
	char buf[8];
	if(uBit.systemTime() - last_rx < 5000)
		return;
	last_rx = uBit.systemTime();
	sprintf(buf,"%03d",rssi);
	uBit.display.scrollAsync(rssi);
}
*/

/* RSSI:
 * 
 * min RSSI: 158
 * max RSSI: ~200
 * 
 * ~5cm distance: 201..204
 * ~1m  distance: 188..191
 * ~3m  distance: 158..172
 * ~5m  distance + wall: 158
 * ~8m distance + 2x wall: 158
 */

void advertisementCallback(const Gap::AdvertisementCallbackParams_t *params) {
    const uint8_t len = params->advertisingDataLen;
	const uint8_t *p = params->advertisingData;
	const uint8_t rssi = params->rssi; /* use for LED brightness */
		
	/* match Exposure Notification Service Class UUID 0xFD6F 
	 * 
	 * spec: https://www.blog.google/documents/70/Exposure_Notification_-_Bluetooth_Specification_v1.2.2.pdf page 4 
	 * 
	 * example:
	 * 03 03 6ffd 
	 * 17 16 6ffd 660a6af67f7e946b3c3ce253dae9b411 78b0e9c2 (rpi, aem)
	 * */
	if((len == 28) && (p[0] == 3) && (p[1] == 3) && (p[2] == 0x6f) && (p[3] == 0xfd)) {
		//last_rssi = rssi;
		//min_rssi = MIN(min_rssi, rssi);

//		rssi_test(rssi);
//		return;

		last_rx = uBit.systemTime();
		uBit.display.printChar('.');
		rxcount++;
	}
}

int main() {
    uBit.ble = new BLEDevice();
    uBit.ble->init();

    uBit.ble->gap().setScanParams(500, 400);
    uBit.ble->gap().startScan(advertisementCallback);

    while (true) {
        //uBit.ble->waitForEvent();
		uBit.sleep(50);
		timer();
		//uBit.sleep(200);
		//uBit.serial.printf("rssi %d min %d\r\n",last_rssi,min_rssi);
    }
    return 0;
}
