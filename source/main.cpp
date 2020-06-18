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

/*
static unsigned long last_rx = 0;
static uint32_t rxcount = 0;

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

struct rpi_s {
	//long last_seen;
	uint16_t short_rpi;
	uint8_t rssi;
	uint8_t active;
	
	/* tiny age-sorted linked list w/o pointers to save precious RAM */
	uint8_t older;
	uint8_t newer;
};

/* TODO: sorted list of last_seen -> rpi entry mapping for entry reuse */
#define RPI_N 			25

static struct rpi_s 	rpi_list[RPI_N];
static uint8_t 			oldest_rpi = 0;
static uint8_t 			newest_rpi = RPI_N - 1;

static void rpi_list_init(void) {
	struct rpi_s *rpi = rpi_list;
	int i;
	
	/* initialize linked list for aging */
	for(i=0; i<RPI_N; i++,rpi++) {
		rpi->older = (i-1)&0xff;
		rpi->newer = i+1;
	}
}

static void refresh_screen(unsigned long now) {
	static unsigned long last_refresh = 0;
	
	if(now - last_refresh < 20)
		return;
	
	else {
		struct rpi_s *rpi = rpi_list;
		uint16_t x,y;
		uint8_t v;

		last_refresh = now;
		//MicroBitImage image(5,5);

		for(x=0;x<5;x++) {
			for(y=0;y<5;y++,rpi++) {
				//v = (now - rpi->last_seen) > 20 ? 0 : 255;
				//image.setPixelValue(x,y,v);
				if(!rpi->active)
					continue;
				rpi->active--;
				if(!rpi->active)
					uBit.display.image.setPixelValue(x,y,0);
			}
		}
		
		//uBit.display.printAsync(image);
	}
}

static void seen(uint16_t short_rpi, uint8_t rssi, unsigned long now) {
	struct rpi_s *rpi = rpi_list;
	uint16_t x,y;
	int idx;
	
	/* try to find rpi in list */
	for(idx=0;(rpi->short_rpi != short_rpi) && (idx<RPI_N);idx++,rpi++) { }
	
	/* allocate rpi if not seen yet */
	if(idx == RPI_N) {
		/* reuse oldest rpi slot */
		idx = oldest_rpi;
		rpi = rpi_list + idx;
		rpi->short_rpi = short_rpi;
	}
	
	/* nothing to do if already newest rpi */
	if(idx != newest_rpi) {
		
		/* remove from chain */
		rpi_list[rpi->newer].older = rpi->older;
		if(idx == oldest_rpi)
			oldest_rpi = rpi->newer;
		else
			rpi_list[rpi->older].newer = rpi->newer;

		/* assign as newest rpi */
		rpi->newer = rpi_list[newest_rpi].newer;
		rpi_list[newest_rpi].newer = idx;
		rpi->older = newest_rpi;
		newest_rpi = idx;
	}
	
	//rpi->last_seen = now;
	rpi->active = 5;
	rpi->rssi = rssi;
	
	x = idx/5;
	y = idx%5;
	uBit.display.image.setPixelValue(x,y,255);
	
	//refresh_screen(now);
}

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

static void exposure_to_uart(const uint8_t *rpi_aem, uint8_t rssi) {
	char buf[64];
	char *p=tohex(buf, rpi_aem, 16);
	*p=' ';
	p=tohex(p+1, rpi_aem+16, 4);
	*p=' ';
	sprintf(p+1,"%03d\r\n",rssi);
	uBit.serial.send(buf, ASYNC);
}

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
static void exposure_rx(const uint8_t *rpi_aem, uint8_t rssi) {
	uint16_t short_rpi = (rpi_aem[0]<<8)|rpi_aem[1];
	unsigned long now = uBit.systemTime();
	
	seen(short_rpi, rssi, now);
	
	/* test */
	/*
	last_rx = now;
	uBit.display.printChar('.');
	rxcount++;
	*/
	
	exposure_to_uart(rpi_aem, rssi);
}

/* see https://os.mbed.com/docs/mbed-os/v5.15/mbed-os-api-doxy/struct_gap_1_1_advertisement_callback_params__t.html */
void advertisementCallback(const Gap::AdvertisementCallbackParams_t *params) {
    const uint8_t len = params->advertisingDataLen;
	const uint8_t *p = params->advertisingData;
	const uint8_t *rpi_aem = p+8;
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
		exposure_rx(rpi_aem, rssi);
	}
}

int main() {
	rpi_list_init();
	
	uBit.serial.setTxBufferSize(64);
	
    uBit.ble = new BLEDevice();
    uBit.ble->init();

    uBit.ble->gap().setScanParams(500, 400);
    uBit.ble->gap().startScan(advertisementCallback);

    while (true) {
        //uBit.ble->waitForEvent();
		uBit.sleep(20);
		//timer();
		refresh_screen(uBit.systemTime());
    }
    return 0;
}
