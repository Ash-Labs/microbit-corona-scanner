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

struct rpi_s {
	uint16_t short_rpi;
	uint8_t rssi;
	uint8_t age;
	
	/* tiny age-sorted linked list w/o pointers to save precious RAM */
	uint8_t older;
	uint8_t newer;
};

#define RPI_N 						25

static struct rpi_s rpi_list[RPI_N];
static uint8_t 		oldest_rpi 		= 0;
static uint8_t 		newest_rpi 		= RPI_N - 1;

static void rpi_list_init(void) {
	struct rpi_s *rpi = rpi_list;
	int i;
	
	/* initialize linked list for aging */
	for(i=0; i<RPI_N; i++,rpi++) {
		rpi->older = (i-1)&0xff;
		rpi->newer = i+1;
		rpi->short_rpi = i;
		rpi->age = 255;
	}
}

MicroBit uBit;

static uint32_t rpis_seen 			= 0;

#define MIN_RSSI					158
#define MIN_BRIGHTNESS				16
#define MAX_BRIGHTNESS				255

#define RPI_AGE_TIMEOUT				(50*2)		/* 50 equals 1 second */
static uint8_t rpi_age_fadeout 		= 5; 		//RPI_AGE_TIMEOUT;

/* config bits */
#define CF_UART_EN					(1<<0)
#define CF_GREYSCALE_EN				(1<<1)
#define CF_FADEOUT_EN				(1<<2)
static uint8_t config				= CF_GREYSCALE_EN;

static uint8_t scale_rssi(uint8_t rssi) {
	uint32_t res = MAX(rssi, MIN_RSSI);
	res -= MIN_RSSI;
	res = MIN(res, 255-MIN_RSSI);
	res*=5;
	res>>=1; /* scale */
	res+=MIN_BRIGHTNESS;
	return MIN(res, MAX_BRIGHTNESS);
}

/* smoothly fade out aged RPIs :) */
static uint8_t calc_brightness(const rpi_s *rpi) {
	uint8_t rssi = rpi->rssi;
	uint8_t age = rpi->age;
	if(age >= rpi_age_fadeout)
		return 0;
	else if(config & CF_GREYSCALE_EN)
		return scale_rssi(rssi);
	else
		return 255;
}

static uint8_t refresh_screen(unsigned long now) {
	struct rpi_s *rpi = rpi_list;
	uint16_t x,y;
	uint8_t rpis_active = 0;

	for(x=0;x<5;x++) {
		for(y=0;y<5;y++,rpi++) {
			if(rpi->age > RPI_AGE_TIMEOUT)
				continue;
			rpis_active++;
			rpi->age++;
			uBit.display.image.setPixelValue(x,y,calc_brightness(rpi));
		}
	}
	return rpis_active;
}

static void seen(uint16_t short_rpi, uint8_t rssi) {
	struct rpi_s *rpi = rpi_list;
	uint16_t x,y;
	int idx;
	
	/* try to find rpi in list */
	for(idx=0;(rpi->short_rpi != short_rpi) && (idx<RPI_N);idx++,rpi++) { }
	
	/* allocate rpi if not seen yet */
	if(idx == RPI_N) {
		rpis_seen++;
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
	
	rpi->rssi = rssi;
	if(!rssi)
		return;
		
	rpi->age = 0;
	
	x = idx/5;
	y = idx%5;
	uBit.display.image.setPixelValue(x,y,calc_brightness(rpi));
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

static void exposure_to_uart(const uint8_t *rpi_aem, uint8_t rssi, unsigned long now) {
	char buf[64];
	char *p = buf;
	//p+=sprintf(buf, "[%10ld] ",now);
	p=tohex(p, rpi_aem, 16);
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
	
	seen(short_rpi, rssi);
	
	if(config & CF_UART_EN)
		exposure_to_uart(rpi_aem, rssi, now);
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

static void greyscale_enable(void) {
	config |= CF_GREYSCALE_EN;
	uBit.display.setDisplayMode(DISPLAY_MODE_GREYSCALE);
}

static void greyscale_disable(void) {
	config &= ~CF_GREYSCALE_EN;
	uBit.display.setDisplayMode(DISPLAY_MODE_BLACK_AND_WHITE);
}

void onButton(MicroBitEvent e) {
	if (e.source == MICROBIT_ID_BUTTON_A)
		config ^= CF_UART_EN;

    if (e.source == MICROBIT_ID_BUTTON_B) {
		if(config & CF_GREYSCALE_EN)
			greyscale_disable();
		else
			greyscale_enable();
	}
}

static void randomize_age(void) {
	uint32_t set=(1<<RPI_N)-1;
	uBit.seedRandom();
	while(set) {
		uint32_t v = uBit.random(RPI_N);
		if(set&(1<<v)) {
			set &= ~(1<<v);
			seen(v, 0);
		}
	}
}

int main() {
	uint32_t now = uBit.systemTime();
	uint32_t last_cntprint = now;	
	//uint32_t nv_rpi_counter = 0, last_nvwrite = now;

	uBit.serial.setTxBufferSize(64);

	rpi_list_init();
	randomize_age();
		
	/* load non-volatile rpi counter (if available) */
	/*
	KeyValuePair* rpi_cnt_storage = uBit.storage.get("rpi_counter");
	if(rpi_cnt_storage)
		memcpy(&nv_rpi_counter, rpi_cnt_storage->value, sizeof(uint32_t));
	rpi_counter = nv_rpi_counter;
	*/
	
	if(config & CF_GREYSCALE_EN)
		greyscale_enable();

    uBit.messageBus.listen(MICROBIT_ID_BUTTON_A, MICROBIT_BUTTON_EVT_LONG_CLICK, onButton);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_B, MICROBIT_BUTTON_EVT_LONG_CLICK, onButton);
	
	btle_set_gatt_table_size(BLE_GATTS_ATTR_TAB_SIZE_MIN);
	
    uBit.ble = new BLEDevice();
    uBit.ble->init();

    uBit.ble->gap().setScanParams(500, 400);
    uBit.ble->gap().startScan(advertisementCallback);

    while (true) {
		uint8_t rpis_active = 0;
		
        //uBit.ble->waitForEvent();
		
		uBit.sleep(20);
		
		now = uBit.systemTime();
		rpis_active = refresh_screen(now);

#if 0		
		/* update non-volatile rpi counter at most once per second */
		if((rpi_counter > nv_rpi_counter) && ((now - last_nvwrite) > 1000)) {
			nv_rpi_counter = rpi_counter;
			last_nvwrite = now;
			// doesn't work yet?
			//uBit.storage.put("rpi_counter", (uint8_t *)&nv_rpi_counter, sizeof(uint32_t));
		}
#endif
		
		/* output rpi counter every 10 seconds */
		if((now - last_cntprint) >= 10000) {
			char buf[48];
			last_cntprint = now;
			sprintf(buf,"RPIs active: %2d seen: %ld\r\n",rpis_active, (unsigned long)rpis_seen);
			uBit.serial.send(buf, ASYNC);
		}
    }
    return 0;
}
