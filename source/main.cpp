#include "MicroBitCustomConfig.h"
#include "MicroBit.h"
#include "ble/DiscoveredCharacteristic.h"
#include "ble/DiscoveredService.h"
#include "is31fl3738.h"

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

#define VERSION_STRING	"v0.6-ext_leds-dev1"

static const uint8_t gamma_lut[] __attribute__ ((aligned (4))) = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0b,0x0d,0x0f,0x11,0x13,0x16,
	0x1a,0x1c,0x1d,0x1f,0x22,0x25,0x28,0x2e,0x34,0x38,0x3c,0x40,0x44,0x48,0x4b,0x4f,
	0x55,0x5a,0x5f,0x64,0x69,0x6d,0x72,0x77,0x7d,0x80,0x88,0x8d,0x94,0x9a,0xa0,0xa7,
	0xac,0xb0,0xb9,0xbf,0xc6,0xcb,0xcf,0xd6,0xe1,0xe9,0xed,0xf1,0xf6,0xfa,0xfe,0xff
};

#define GAMMA_CORRECT(a)	(gamma_lut[(a)>>2])

#define RAND_BDADDR_TYPE(a)      ((a)[5]>>6)
enum {
	RAND_BDADDR_NONRESOLVABLE       = 0,
	RAND_BDADDR_RESOLVABLE          = 1,
	RAND_BDADDR_TYPE_2				= 2,
	RAND_BDADDR_STATIC				= 3
};

MicroBit uBit;

static uint32_t rpis_seen           = 0; /* includes apple */
static uint32_t apple_rpis_seen     = 0;

static uint8_t click_request        = 0;
static uint8_t strongest_rpi        = UINT8_MAX;

#define MIN_RSSI					(-98)
#define MAX_RSSI					(-56)
#define MIN_BRIGHTNESS				16
#define MAX_BRIGHTNESS				UINT8_MAX

#define RPI_AGE_TIMEOUT				(50*2)				/* 50 equals 1 second */
static uint8_t age_fadeout_apple    = RPI_AGE_TIMEOUT;
static uint8_t age_fadeout_other    = RPI_AGE_TIMEOUT;

/* config bits */
#define CF_UART_EN					(1<<0)	/* enable USB serial RPI output */
#define CF_RSSI_BRIGHTNESS			(1<<1)	/* use RSSI for LED brightness 	*/
#define CF_PERSISTENCE_EN			(1<<2)	/* persistence visualisation 	*/
#define CF_FADEOUT_EN				(1<<3)	/* fadeout LEDs over time 		*/
#define CF_DEVTYPE_VISUALIZE		(1<<4)	/* device type visualisation 	*/
#define CF_CLICK_EN					(1<<5)	/* Audio clicks enable 			*/
#define CF_EXTLEDS_EN				(1<<6)	/* use external LEDs			*/
static uint8_t config				= 0;

struct rpi_s {
	uint16_t short_rpi;
	uint8_t devtype_rssi;
	uint8_t age;
	
	/* tiny age-sorted linked list w/o pointers to save precious RAM */
	uint8_t older;
	uint8_t newer;
};

#define RPI_DEVICE_APPLE(a)         ((a)->devtype_rssi>>7)
#define RPI_RSSI(a)                 ((a)->devtype_rssi|0x80)

#define RPI_N                       25

static struct rpi_s rpi_list[RPI_N];
static uint8_t 		oldest_rpi 		= 0;
static uint8_t 		newest_rpi 		= RPI_N - 1;

static void rpi_list_init(void) {
	struct rpi_s *rpi = rpi_list;
	int i;
	
	/* initialize linked list for aging */
	for(i=0; i<RPI_N; i++,rpi++) {
		rpi->older = (i-1)&UINT8_MAX;
		rpi->newer = i+1;
		rpi->short_rpi = i;
		rpi->age = UINT8_MAX;
	}
}

static uint8_t scale_rssi(int8_t rssi) {
	int32_t res = MAX(rssi, MIN_RSSI);  /* RSSI lower limits */
	res = MIN(rssi, MAX_RSSI);          /* RSSI upper limit */
	res -= MIN_RSSI;                    /* -98 .. -56 -> 0 .. 42 */
	res*=23;                            /* scale to 0 .. 966 */
	res>>=2;                            /* scale to 0 .. 241 */
	res+=MIN_BRIGHTNESS;                /* 16 .. 257 */
	return MIN(res, MAX_BRIGHTNESS);    /* clamp to 255 */
}

/* smoothly fade out aged RPIs :) */
static uint8_t calc_brightness(const rpi_s *rpi, unsigned long now) {
	uint8_t age_fadeout = RPI_DEVICE_APPLE(rpi) ? age_fadeout_apple : age_fadeout_other;
	uint8_t val, age = rpi->age;

	if(age >= age_fadeout)
		return 0;

	val = config & CF_RSSI_BRIGHTNESS ? scale_rssi(RPI_RSSI(rpi)) : UINT8_MAX;

	/* add 2Hz on/off blinking for resolvable devices if persistence mode and non-/resolvable visualisation enabled */
	if((config & CF_PERSISTENCE_EN) && (config & CF_DEVTYPE_VISUALIZE) && (!RPI_DEVICE_APPLE(rpi))) {
		uint32_t fraction = now&511;
		if(fraction > 255)
			return 0;
	}
	
	if(config & CF_FADEOUT_EN) {
		uint32_t v32 = val;
		v32 *= RPI_AGE_TIMEOUT-age;
		v32 /= RPI_AGE_TIMEOUT;
		val = v32;
	}
	return GAMMA_CORRECT(val);
}

static void set_pixel(int16_t x , int16_t y, uint8_t value) {
	uBit.display.image.setPixelValue(x,y,value);
	if(config & CF_EXTLEDS_EN)
		is31fl3738_setPixel(x,y,value);
}

static uint8_t refresh_screen(unsigned long now, uint8_t *apple_rpis_active) {
	struct rpi_s *rpi = rpi_list;
	uint8_t apple_rpis = 0, rpis = 0, _strongest_rpi = UINT8_MAX;
	int8_t rssi, best_rssi = INT8_MIN;
	uint16_t x,y;
	
	for(x=0;x<5;x++) {
		for(y=0;y<5;y++,rpi++) {
			
			/* ignore dead RPIs */
			if(rpi->age > RPI_AGE_TIMEOUT)
				continue;

			/* update active RPIs counter */
			rpis++;
			apple_rpis += RPI_DEVICE_APPLE(rpi);

			rpi->age++;

			set_pixel(x,y,calc_brightness(rpi, now));

			/* find RPI with highest RSSI */
			rssi = RPI_RSSI(rpi);
			if(rssi>best_rssi) {
				best_rssi = rssi;
				_strongest_rpi = rpi - rpi_list;
			}
		}
	}

	strongest_rpi = _strongest_rpi;

	if(apple_rpis_active)
		*apple_rpis_active = apple_rpis;

	return rpis;
}

static uint8_t seen(uint16_t short_rpi, int8_t rssi, const uint8_t *peer_addr, uint8_t flags_present) {
	struct rpi_s *rpi = rpi_list;
	uint16_t x,y;
	uint8_t likely_apple;
	int idx;
	
	/* try to find rpi in list */
	for(idx=0;(rpi->short_rpi != short_rpi) && (idx<RPI_N);idx++,rpi++) { }
	
	/* allocate rpi if not seen yet */
	if(idx == RPI_N) {
		rpis_seen++;
		apple_rpis_seen += ((RAND_BDADDR_TYPE(peer_addr) == RAND_BDADDR_NONRESOLVABLE) && flags_present);
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

	if(!peer_addr)
		return 0;

	likely_apple = (RAND_BDADDR_TYPE(peer_addr) == RAND_BDADDR_NONRESOLVABLE) && flags_present;
	rssi = MIN(rssi, -1); /* clamp to -1 (negative numbers only) */
	rpi->devtype_rssi = (likely_apple<<7)|(rssi&0x7f);

	rpi->age = 0;

	x = idx/5;
	y = idx%5;
	set_pixel(x,y,calc_brightness(rpi, uBit.systemTime()));

	return idx == strongest_rpi;
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

static void exposure_to_uart(const uint8_t *rpi_aem, int8_t rssi, const uint8_t *peer_addr, uint8_t flags_present, uint8_t is_strongest) {
	char buf[64], *p=buf;
	/*
	char buf[80], *p=buf;
	p=tohex(buf, peer_addr, 6);
	p=' ';
	p++;
	*/
	p=tohex(p, rpi_aem, 16);
	*p=' ';
	p=tohex(p+1, rpi_aem+16, 4);
	*p=' ';
	sprintf(p+1, "%c%c%c %03d\r\n", '0'+RAND_BDADDR_TYPE(peer_addr), flags_present ? 'F' : ' ', is_strongest ? '!' : ' ', rssi);
	uBit.serial.send(buf, ASYNC);
}

/* RSSI:
 * 
 * min RSSI: -98
 * max RSSI: approx. -56
 * 
 * ~5cm distance: -55 .. -52
 * ~1m  distance: -68 .. -65
 * ~3m  distance: -98 .. -84
 * ~5m  distance + wall: -98
 * ~8m distance + 2x wall: -98
 */
static void exposure_rx(const uint8_t *rpi_aem, int8_t rssi, const uint8_t *peer_addr, uint8_t flags_present) {
	uint16_t short_rpi = (rpi_aem[0]<<8)|rpi_aem[1];
	uint8_t is_strongest = seen(short_rpi, rssi, peer_addr, flags_present);
	
	click_request += (is_strongest ^ 1);
	
	if((config & CF_UART_EN) && (uBit.serial.txBufferedSize() <= 64)) /* prevent garbled lines */
		exposure_to_uart(rpi_aem, rssi, peer_addr, flags_present, is_strongest);
}

/* see https://os.mbed.com/docs/mbed-os/v5.15/mbed-os-api-doxy/struct_gap_1_1_advertisement_callback_params__t.html */
void advertisementCallback(const Gap::AdvertisementCallbackParams_t *params) {
    uint8_t len = params->advertisingDataLen;
	const uint8_t *p = params->advertisingData;
	const int8_t rssi = params->rssi; /* use for LED brightness */
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
		exposure_rx(p+8, rssi, params->peerAddr, flags_present);
}

static void audible_click(void) {
	uBit.io.P0.setAnalogValue(512);
	uBit.sleep(1);
	uBit.io.P0.setAnalogValue(0);
}

/* visualisation modes:
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
	age_fadeout_apple = age_fadeout_other = (mode&1) ? 5 : RPI_AGE_TIMEOUT;
	if(config & CF_DEVTYPE_VISUALIZE) {
		age_fadeout_apple = (mode&1) ? 7 : RPI_AGE_TIMEOUT; /* make Apple blinks longer */
		age_fadeout_other = (mode&1) ? 2 : RPI_AGE_TIMEOUT; /* make other blinks shorter */
	}
	
	/* persistence flag */
	if (mode&1)
		config &= ~CF_PERSISTENCE_EN;
	else
		config |= CF_PERSISTENCE_EN;
	
	/* RSSI brightness or full brightness? */
	if (mode&2)
		config &= ~CF_RSSI_BRIGHTNESS;
	else
		config |= CF_RSSI_BRIGHTNESS;
	
	uBit.display.setDisplayMode(config&CF_RSSI_BRIGHTNESS ? DISPLAY_MODE_GREYSCALE : DISPLAY_MODE_BLACK_AND_WHITE);
}

/* button usage:
 * 
 * (long clicks are >= 2 seconds)
 * 
 * A during reset: sequential LED usage instead of randomized
 * 
 * A short click : audio clicks on/off
 * A long click  : enable RPI output via USB serial
 * 
 * B short click : change visualisation mode
 * B long click  : Apple/Other device type visualisation on/off
 * 
 */
void onLongClick(MicroBitEvent e) {
	if (e.source == MICROBIT_ID_BUTTON_A)
		config ^= CF_UART_EN;
	else if (e.source == MICROBIT_ID_BUTTON_B) {
		config ^= CF_DEVTYPE_VISUALIZE;
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
    else if (e.source == MICROBIT_ID_BUTTON_B)
		mode_change(1);
}

static void randomize_age(void) {
	uint32_t set=(1<<RPI_N)-1;
	uBit.seedRandom();
	while(set) {
		uint32_t v = uBit.random(RPI_N);
		if(set&(1<<v)) {
			set &= ~(1<<v);
			seen(v, INT8_MIN, NULL, 0); /* short_rpis must be preinitialized to 0..RPI_N or read from NULL ptr will be triggered! */
		}
	}
}

/* TODO:
 * 
 * - serial: support serial commands? (e.g. RPI-to-UART en/disable?)
 * - audio: mute clicks from oldest RPI or RPI with highest seen counter instead of highest RSSI?
 * - visual: map age/seen counter to LED position?
 * - visual: stretch fadeout from RSSI to zero in RSSI-mode?
 * 
 */
int main() {
	uint32_t now = uBit.systemTime();
	uint32_t last_cntprint = now;	
	uint8_t rpis_active, apple_rpis_active, clicks_done = 0, sleep_time = 20;

	uBit.serial.setTxBufferSize(128);

	rpi_list_init();
	
	if(!uBit.buttonA.isPressed())
		randomize_age();
	
	mode_change(0);
	
	/* display project identifier and version string both via LEDs and USB serial */
	uBit.display.scroll("cs-" VERSION_STRING, MICROBIT_DEFAULT_SCROLL_SPEED/2);
	uBit.serial.send("corona-scanner " VERSION_STRING "\r\n", SYNC_SPINWAIT);

	if(is31fl3738_init() == MICROBIT_OK) {
		uBit.serial.send("using IS31FL3738 output\r\n", SYNC_SPINWAIT);
		config |= CF_EXTLEDS_EN;
	}

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
		
		uBit.sleep(sleep_time);
				
		now = uBit.systemTime();
		rpis_active = refresh_screen(now, &apple_rpis_active);
		
		/* output rpi counter every 10 seconds */
		if(((now - last_cntprint) >= 10000) && (uBit.serial.txBufferedSize() <= 16)) {
			char buf[128];
			last_cntprint = now;
			sprintf(buf,"RPIs active: %2d (Apple: %2d, Google: %2d) seen: %ld (Apple: %ld, Google: %ld)\r\n",
				rpis_active, apple_rpis_active, rpis_active - apple_rpis_active,
				rpis_seen,   apple_rpis_seen,   rpis_seen   - apple_rpis_seen);
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
