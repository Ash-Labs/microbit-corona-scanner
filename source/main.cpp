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

#define VERSION_STRING	"v0.4-dev"

struct rpi_s {
	uint16_t short_rpi;
	uint8_t flags_rssi;				/* flags present: 0x80, RSSI mask: 0x7f */
	uint8_t age;
	
	/* tiny age-sorted linked list w/o pointers to save precious RAM */
	uint8_t older;
	uint8_t newer;
};

#define HAS_FLAGS(a) 				((a)>>7)
#define GET_RSSI(a)					(((a)&0x7f)+128)

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

static uint32_t google_rpis_seen	= 0;
static uint32_t apple_rpis_seen		= 0;
static uint8_t click_request		= 0;
static uint8_t muted_rpi			= 255;

#define MIN_RSSI					158
#define MIN_BRIGHTNESS				16
#define MAX_BRIGHTNESS				255

#define RPI_AGE_TIMEOUT				(50*2)				/* 50 equals 1 second */
static uint8_t apple_age_fadeout 	= RPI_AGE_TIMEOUT;
static uint8_t google_age_fadeout	= RPI_AGE_TIMEOUT;

/* config bits */
#define CF_UART_EN					(1<<0)	/* enable USB serial RPI output */
#define CF_RSSI_BRIGHTNESS			(1<<1)	/* use RSSI for LED brightness 	*/
#define CF_FADEOUT_EN				(1<<2)	/* fadeout LEDs over time 		*/
#define CF_CLICK_EN					(1<<3)	/* Audio clicks enable 			*/
#define CF_GOOPLE_VISUALIZE			(1<<4)	/* Apple/Google visualisation 	*/
static uint8_t config				= 0;

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
	uint8_t val, age = rpi->age;
	uint8_t age_fadeout = HAS_FLAGS(rpi->flags_rssi) ? apple_age_fadeout : google_age_fadeout;
	
	if(age >= age_fadeout)
		return 0;
	
	val = config & CF_RSSI_BRIGHTNESS ? scale_rssi(GET_RSSI(rpi->flags_rssi)) : 255;
	
	/* TODO: add on/off modulation for Apple/Google distinction? */
	
	if(config & CF_FADEOUT_EN) {
		uint32_t v32 = val;
		v32 *= RPI_AGE_TIMEOUT-age;
		v32 /= RPI_AGE_TIMEOUT;
		val = v32;
	}
	return val;
}

static uint8_t refresh_screen(unsigned long now, uint8_t *apple_rpis_active, uint8_t *google_rpis_active) {
	struct rpi_s *rpi = rpi_list;
	uint16_t x,y;
	uint8_t apple_rpis = 0, google_rpis = 0, flags;
	uint8_t rssi, best_rssi = 0, best_rpi = 255;

	for(x=0;x<5;x++) {
		for(y=0;y<5;y++,rpi++) {
			
			/* ignore dead RPIs */
			if(rpi->age > RPI_AGE_TIMEOUT)
				continue;
			
			/* update active RPIs counter */
			flags = HAS_FLAGS(rpi->flags_rssi);
			apple_rpis += flags;
			google_rpis += (flags^1);
			
			rpi->age++;
			
			uBit.display.image.setPixelValue(x,y,calc_brightness(rpi));
			
			/* find RPI with highest RSSI */
			rssi = GET_RSSI(rpi->flags_rssi);
			if(rssi>best_rssi) {
				best_rssi = rssi;
				best_rpi = rpi - rpi_list;
			}
		}
	}
	
	muted_rpi = best_rpi;
	
	if(apple_rpis_active)
		*apple_rpis_active = apple_rpis;
	
	if(google_rpis_active)
		*google_rpis_active = google_rpis;
	
	return apple_rpis + google_rpis;
}

static void seen(uint16_t short_rpi, uint8_t rssi, uint8_t flags_present) {
	struct rpi_s *rpi = rpi_list;
	uint16_t x,y;
	int idx;
	
	/* try to find rpi in list */
	for(idx=0;(rpi->short_rpi != short_rpi) && (idx<RPI_N);idx++,rpi++) { }
	
	/* allocate rpi if not seen yet */
	if(idx == RPI_N) {
		apple_rpis_seen += flags_present;
		google_rpis_seen += (flags_present^1);
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

	if(!rssi) {
		rpi->flags_rssi = 0;
		return;
	}
	
	rssi = (rssi > 128) ? (rssi-128) : 1;
	rpi->flags_rssi = (flags_present<<7)|rssi;
	
	rpi->age = 0;
	
	x = idx/5;
	y = idx%5;
	uBit.display.image.setPixelValue(x,y,calc_brightness(rpi));
	
	click_request += (idx != muted_rpi);
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

static void exposure_to_uart(const uint8_t *rpi_aem, uint8_t rssi, unsigned long now, uint8_t flags_present) {
	char buf[64];
	char *p = buf;
	p=tohex(p, rpi_aem, 16);
	*p=' ';
	p=tohex(p+1, rpi_aem+16, 4);
	*p=' ';
	sprintf(p+1,"%c%c %03d\r\n",flags_present ? 'A' : 'G',' ',rssi); /* unused flag: RFU: indicator for RPI w/ highest RSSI */
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
static void exposure_rx(const uint8_t *rpi_aem, uint8_t rssi, uint8_t flags_present) {
	uint16_t short_rpi = (rpi_aem[0]<<8)|rpi_aem[1];
	unsigned long now = uBit.systemTime();
	
	seen(short_rpi, rssi, flags_present);
	
	if(config & CF_UART_EN)
		exposure_to_uart(rpi_aem, rssi, now, flags_present);
}

/* see https://os.mbed.com/docs/mbed-os/v5.15/mbed-os-api-doxy/struct_gap_1_1_advertisement_callback_params__t.html */
void advertisementCallback(const Gap::AdvertisementCallbackParams_t *params) {
    uint8_t len = params->advertisingDataLen;
	const uint8_t *p = params->advertisingData;
	const uint8_t rssi = params->rssi; /* use for LED brightness */
	uint8_t flags_present = 0;
	
	/* match Exposure Notification Service Class UUID 0xFD6F 
	 * 
	 * spec: https://www.blog.google/documents/70/Exposure_Notification_-_Bluetooth_Specification_v1.2.2.pdf page 4 
	 * 
	 * example:
	 * 03 03 6ffd 
	 * 17 16 6ffd 660a6af67f7e946b3c3ce253dae9b411 78b0e9c2 (rpi, aem)
	 * */
	
	/* 02 01 1a only sent by iOS !??? */
	if((len == 31) && (p[0] == 2) && (p[1] == 1) && (p[2] == 0x1a)) {
		p+=3;
		len-=3;
		flags_present = 1;
	}
	
	if((len == 28) && (p[0] == 3) && (p[1] == 3) && (p[2] == 0x6f) && (p[3] == 0xfd))
		exposure_rx(p+8, rssi, flags_present);
}

static void audible_click(void) {
	uBit.io.P0.setAnalogValue(512);
	uBit.sleep(1);
	uBit.io.P0.setAnalogValue(0);
}

/* modes:
 * 0: persistence with fadeout from RSSI				[DEFAULT]
 * 1: blink with RSSI brightness
 * 2: persistence at full brightness
 * 3: blink at full brightness
 */
static void mode_change(uint8_t inc) {
	static uint8_t mode = 0;
	
	mode+=inc;
	mode&=3;

	/* fadeout? */
	if(!mode)
		config |= CF_FADEOUT_EN;
	else
		config &= ~CF_FADEOUT_EN;
	
	/* short blinks vs. inactive after 2 seconds */
	apple_age_fadeout = google_age_fadeout = (mode&1) ? 5  : RPI_AGE_TIMEOUT;
	if(config & CF_GOOPLE_VISUALIZE)
		google_age_fadeout = (mode&1) ? 2 : RPI_AGE_TIMEOUT; /* make Google blinks shorter */
	
	/* RSSI brightness or full brightness? */
	if (mode&2)
		config &= ~CF_RSSI_BRIGHTNESS;
	else
		config |= CF_RSSI_BRIGHTNESS;
	
	uBit.display.setDisplayMode(config&CF_RSSI_BRIGHTNESS ? DISPLAY_MODE_GREYSCALE : DISPLAY_MODE_BLACK_AND_WHITE);
}

void onLongClick(MicroBitEvent e) {
	if (e.source == MICROBIT_ID_BUTTON_A)
		config ^= CF_UART_EN;
	if (e.source == MICROBIT_ID_BUTTON_B) {
		config ^= CF_GOOPLE_VISUALIZE;
		mode_change(0);
	}
}

void onClick(MicroBitEvent e) {
	if (e.source == MICROBIT_ID_BUTTON_A) {
		config ^= CF_CLICK_EN;
		audible_click();
		if(config & CF_CLICK_EN) /* click twice to signal clicks enabled */
			click_request++;
	}

    if (e.source == MICROBIT_ID_BUTTON_B)
		mode_change(1);
}

static void randomize_age(void) {
	uint32_t set=(1<<RPI_N)-1;
	uBit.seedRandom();
	while(set) {
		uint32_t v = uBit.random(RPI_N);
		if(set&(1<<v)) {
			set &= ~(1<<v);
			seen(v, 0, 0);
		}
	}
}

/* TODO:
 * - input: button remap and documentation
 * - visual: Apple/Google visualisation
 * -> v0.4
 * 
 * future:
 * - audio: mute clicks from oldest RPI or RPI with highest seen counter instead of highest RSSI?
 * - visual: map age to LED position?
 * - visual: change RSSI -> brightness mapping?
 * - visual: stretch fadeout from RSSI to zero in RSSI-mode?
 * - visual: gamma correction?
 * 
*/
int main() {
	uint32_t now = uBit.systemTime();
	uint32_t last_cntprint = now;	
	uint8_t clicks_done = 0, sleep_time = 20;

	uBit.serial.setTxBufferSize(128);

	rpi_list_init();
	
	if(!uBit.buttonA.isPressed())
		randomize_age();
	
	mode_change(0);
	
	/* display project identifier and version string both via LEDs and USB serial */
	uBit.display.scroll("cs-" VERSION_STRING, MICROBIT_DEFAULT_SCROLL_SPEED/2);
	uBit.serial.send("corona-scanner " VERSION_STRING "\r\n", SYNC_SPINWAIT);

    uBit.messageBus.listen(MICROBIT_ID_BUTTON_A, MICROBIT_BUTTON_EVT_CLICK, onClick);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_B, MICROBIT_BUTTON_EVT_CLICK, onClick);
	uBit.messageBus.listen(MICROBIT_ID_BUTTON_A, MICROBIT_BUTTON_EVT_LONG_CLICK, onLongClick);
	uBit.messageBus.listen(MICROBIT_ID_BUTTON_B, MICROBIT_BUTTON_EVT_LONG_CLICK, onLongClick);
	
	btle_set_gatt_table_size(BLE_GATTS_ATTR_TAB_SIZE_MIN);
	
    uBit.ble = new BLEDevice();
    uBit.ble->init();

    uBit.ble->gap().setScanParams(500, 400);
    uBit.ble->gap().startScan(advertisementCallback);

	uBit.io.P0.setAnalogValue(0);
	uBit.io.P0.setAnalogPeriodUs(1000000/1000);
	
	/* do a dummy click */	
	click_request++;

    while (true) {
		uint8_t apple_rpis_active = 0, google_rpis_active = 0;
		
		uBit.sleep(sleep_time);
				
		now = uBit.systemTime();
		refresh_screen(now, &apple_rpis_active, &google_rpis_active);
		
		/* output rpi counter every 10 seconds */
		if((now - last_cntprint) >= 10000) {
			char buf[128];
			last_cntprint = now;
			sprintf(buf,"RPIs active: %2d (Apple: %2d, Google: %2d) seen: %ld (Apple: %ld, Google: %ld)\r\n",
				apple_rpis_active + google_rpis_active, apple_rpis_active, google_rpis_active,
				apple_rpis_seen + google_rpis_seen, apple_rpis_seen, google_rpis_seen);
			uBit.serial.send(buf, ASYNC);
		}
		
		sleep_time = 20;
		if(clicks_done != click_request) {
			clicks_done = click_request;
			if(config & CF_CLICK_EN) {
				audible_click();
				sleep_time = 19;
			}
		}
    }
    return 0;
}
