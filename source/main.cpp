#include "MicroBitCustomConfig.h"
#include "MicroBit.h"
#include "ble/DiscoveredCharacteristic.h"
#include "ble/DiscoveredService.h"
#include "config_flags.h"
#include "is31fl3738.h"
#include "audio.h"

#include <inttypes.h>

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

#define VERSION_STRING	"v0.6-dev10"

static const uint8_t gamma_lut[] __attribute__ ((aligned (4))) = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0b,0x0d,0x0f,0x11,0x13,0x16,
	0x1a,0x1c,0x1d,0x1f,0x22,0x25,0x28,0x2e,0x34,0x38,0x3c,0x40,0x44,0x48,0x4b,0x4f,
	0x55,0x5a,0x5f,0x64,0x69,0x6d,0x72,0x77,0x7d,0x80,0x88,0x8d,0x94,0x9a,0xa0,0xa7,
	0xac,0xb0,0xb9,0xbf,0xc6,0xcb,0xcf,0xd6,0xe1,0xe9,0xed,0xf1,0xf6,0xfa,0xfe,0xff
};

#define GAMMA_CORRECT(a)            (gamma_lut[(a)>>2])

#define RAND_BDADDR_TYPE(a)         ((a)[5]>>6)
enum {
	RAND_BDADDR_NONRESOLVABLE       = 0,
	RAND_BDADDR_RESOLVABLE          = 1,
	RAND_BDADDR_TYPE_2				= 2,
	RAND_BDADDR_STATIC				= 3
};

MicroBit uBit;
MicroBitI2C *i2c_bus                = &uBit.i2c;

static uint32_t bds_seen            = 0;
static uint32_t rpis_seen           = 0;

static uint8_t audio_request        = 0;
static uint8_t strongest_bd         = UINT8_MAX;

#define MIN_RSSI					(-98)
#define MAX_RSSI					(-56)
#define MIN_BRIGHTNESS				16
#define MAX_BRIGHTNESS				UINT8_MAX

#define REFRESH_HZ                  50
#define REFRESH_DELAY               (1000/REFRESH_HZ)    /* system time is in milliseconds */

#define BD_AGE_TIMEOUT				(REFRESH_HZ*2)       /* REFRESH_HZ equals 1 second */
static uint8_t bd_age_fadeout        = BD_AGE_TIMEOUT;
static uint8_t rpi_age_fadeout       = BD_AGE_TIMEOUT;

#define THRASHING_LOCKOUT           (BD_AGE_TIMEOUT*2)  /* 4 seconds (200 < UINT8_MAX) */
static uint8_t thrashing_likely      = 0;

uint16_t config                      = 0;

struct bd_s {
	uint16_t short_id;
	uint8_t type_rssi;
	uint8_t age;
	
	/* tiny age-sorted linked list w/o pointers to save precious RAM */
	uint8_t older;
	uint8_t newer;
};

#define BTN_CLICK_THRESHOLD         (2)
#define BTN_LONGCLICK_THRESHOLD     (REFRESH_HZ+(REFRESH_HZ/2))

#define BTN_A_PRESSED()		        uBit.buttonA.isPressed()
#define BTN_B_PRESSED()		        (config & CF_HW_CALLIOPE ? (!uBit.io.P16.getDigitalValue()) : uBit.buttonB.isPressed())

#define UART_TXBUFSZ                128
#define UART_CANQUEUE(a)            ((UART_TXBUFSZ - uBit.serial.txBufferedSize()) >= (int)(a))

#define BD_TYPE_RPI(a)              ((a)->type_rssi>>7)
#define BD_RSSI(a)                  ((a)->type_rssi|0x80)

#define BD_N                        25

static struct bd_s bd_list[BD_N];
static uint8_t 		oldest_bd 		= 0;
static uint8_t 		newest_bd 		= BD_N - 1;

static void bd_list_init(void) {
	struct bd_s *bd = bd_list;
	int i;
	
	/* initialize linked list for aging */
	for(i=0; i<BD_N; i++,bd++) {
		bd->older = (i-1)&UINT8_MAX;
		bd->newer = i+1;
		bd->short_id = i;
		bd->age = UINT8_MAX;
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
static uint8_t calc_brightness(const bd_s *bd, unsigned long now) {
	uint8_t fadeout = BD_TYPE_RPI(bd) ? rpi_age_fadeout : bd_age_fadeout;
	uint8_t val, age = bd->age;

	if(age >= fadeout)
		return 0;

	val = config & CF_RSSI_BRIGHTNESS ? scale_rssi(BD_RSSI(bd)) : UINT8_MAX;

	/* add ~2Hz on/off blinking for exposure notifications if persistence mode and ALLBLE enabled */
	if((config & CF_PERSISTENCE_EN) && (config & CF_ALLBLE_EN) && (!(config & CF_RPIS_DONTBLINK)) && (BD_TYPE_RPI(bd)) && (now&0x100))
		return 0;

	if(config & CF_FADEOUT_EN) {
		uint32_t v32 = val;
		v32 *= BD_AGE_TIMEOUT-age;
		v32 /= BD_AGE_TIMEOUT;
		val = v32;
	}
	return GAMMA_CORRECT(val);
}

static void set_pixel(int16_t x , int16_t y, uint8_t value) {
	uBit.display.image.setPixelValue(x,y,value);
	if(config & CF_EXTLEDS_EN)
		is31fl3738_setPixel(x,y,value);
}

static void update_display(void) {
	if(config & CF_EXTLEDS_EN)
		is31fl3738_update();
}

static uint8_t refresh_screen(unsigned long now, uint8_t *rpis_active) {
	struct bd_s *bd = bd_list;
	uint8_t rpis = 0, bds = 0, _strongest_bd = UINT8_MAX;
	int8_t rssi, best_rssi = INT8_MIN;
	uint16_t x,y;
	
	for(y=0;y<5;y++) {
		for(x=0;x<5;x++,bd++) {
			
			/* ignore dead entries */
			if(bd->age > BD_AGE_TIMEOUT)
				continue;

			/* update active counters */
			bds++;
			rpis += BD_TYPE_RPI(bd);

			bd->age++;

			set_pixel(x,y,calc_brightness(bd, now));

			/* find BD with highest RSSI */
			rssi = BD_RSSI(bd);
			if(rssi>best_rssi) {
				best_rssi = rssi;
				_strongest_bd = bd - bd_list;
			}
		}
	}

	update_display();

	strongest_bd = _strongest_bd;

	if(rpis_active)
		*rpis_active = rpis;

	return bds;
}

static uint8_t seen(const uint8_t *id_data, int8_t rssi, uint8_t type) {
	struct bd_s *bd = bd_list;
	uint16_t x, y, short_id = (id_data[1]<<8)|id_data[0];
	int idx;

	/* try to find bd in list */
	for(idx=0;(bd->short_id != short_id) && (idx<BD_N);idx++,bd++) { }

	/* allocate bd if not seen yet */
	if(idx == BD_N) {

		/* prepare to reclaim oldest BD entry */
		idx = oldest_bd;
		bd = bd_list + idx;

		/* thrashing prevention: only reclaim oldest entry if BD no longer active
		 * However, without this blinking is more 'agitated' in situations with lots of devices.
		 * I think this is a more suitable visualisation for these cases.
		 * Therefore this thrashing prevention is disabled for now. */
		/*
		if(bd->age <= BD_AGE_TIMEOUT)
			return 0;
		*/

		/* claim & reassign BD entry */
		bd->short_id = short_id;

		/* prevent inaccurate huge seen counter readings if more than BD_N active BDs are seen */
		if(!thrashing_likely) {
			bds_seen++;
			rpis_seen += type;
		}
	}
	
	/* nothing to do if already newest bd */
	if(idx != newest_bd) {
		
		/* remove from chain */
		bd_list[bd->newer].older = bd->older;
		if(idx == oldest_bd)
			oldest_bd = bd->newer;
		else
			bd_list[bd->older].newer = bd->newer;

		/* assign as newest bd */
		bd->newer = bd_list[newest_bd].newer;
		bd_list[newest_bd].newer = idx;
		bd->older = newest_bd;
		newest_bd = idx;
	}

	if(rssi == INT8_MIN)
		return 0;

	rssi = MIN(rssi, -1); /* clamp to -1 (negative numbers only) */
	bd->type_rssi = (type<<7)|(rssi&0x7f);

	bd->age = 0;

	y = idx/5;
	x = idx%5;
	set_pixel(x,y,calc_brightness(bd, uBit.systemTime()));

	return idx == strongest_bd;
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

static void raw_to_uart(const uint8_t *d, uint8_t len, const uint8_t *paddr, int8_t rssi) {
	char buf[128], *p;

	if((len>(sizeof(buf)-21)) || (!UART_CANQUEUE(len*2+21)))
		return;

	p=tohex(buf, paddr, 6);
	*p=' ';
	p=tohex(p+1, d, len);
	*p=' ';
	sprintf(p+1, "%03d\r\n", rssi);
	uBit.serial.send(buf, ASYNC);
}

static void exposure_to_uart(const uint8_t *rpi_aem, int8_t rssi, const uint8_t *peer_addr, int adv_flags, uint8_t is_strongest) {
	char buf[60], *p=buf;

	if(!UART_CANQUEUE(sizeof(buf))) /* prevent garbled lines */
		return;

	p=tohex(p, rpi_aem, 16);
	*p=' ';

	p=tohex(p+1, rpi_aem+16, 4);
	*p=' ';

	p[1]='0'+RAND_BDADDR_TYPE(peer_addr);

	if(adv_flags >= 0) {
		p[2]=nibble2hex(adv_flags>>4);
		p[3]=nibble2hex(adv_flags&0xf);
	}
	else
		p[2]=p[3]='-';

	p[4]=' ';
	sprintf(p+5, "%03d%c\r\n", rssi, is_strongest ? '!' : ' ');
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

/* see https://os.mbed.com/docs/mbed-os/v5.15/mbed-os-api-doxy/struct_gap_1_1_advertisement_callback_params__t.html */
void advertisementCallback(const Gap::AdvertisementCallbackParams_t *params) {
    uint8_t len = params->advertisingDataLen;
	const uint8_t *p = params->advertisingData;
	const uint8_t *peer_addr = params->peerAddr;
	const int8_t rssi = params->rssi; /* use for LED brightness */
	const uint8_t *id_data;
	uint8_t is_strongest = 0;
	int exposure_notification, adv_flags = -1;
	
	/* match Exposure Notification Service Class UUID 0xFD6F 
	 * 
	 * spec: https://www.blog.google/documents/70/Exposure_Notification_-_Bluetooth_Specification_v1.2.2.pdf page 4 
	 * 
	 * example:
	 * 03 03 6ffd 
	 * 17 16 6ffd 660a6af67f7e946b3c3ce253dae9b411 78b0e9c2 (rpi, aem)
	 * */

	/* flags section present? */
	if((len >= 31) && (p[0] == 2) && (p[1] == 1)) {
		adv_flags = p[2];
		p+=3;
		len-=3;
	}

	/* figure out if this is a COVID-19 Exposure Notification */
	exposure_notification = (len >= 28) && (p[0] == 3) && (p[1] == 3) && (p[2] == 0x6f) && (p[3] == 0xfd);

	/* drop data unless exposure notification or CF_ALLBLE_EN */
	if((!exposure_notification) && (!(config & CF_ALLBLE_EN)))
		return;

	id_data = exposure_notification ? p+8 : peer_addr;

	/* keep track of this BD if Exposure Notification or unfiltered BLE mode enabled */
	is_strongest = seen(id_data, rssi, exposure_notification);
	audio_request += (is_strongest ^ 1);

	/* forward via UART if enabled */
	if(!(config & CF_UART_EN))
		return;

	if(config & CF_ALLBLE_EN)
		raw_to_uart(params->advertisingData, params->advertisingDataLen, peer_addr, rssi);
	else
		exposure_to_uart(id_data, rssi, peer_addr, adv_flags, is_strongest);
}

static void audio_mode_change(void) {

	if(!(config & CF_HW_CALLIOPE))
		config ^= CF_AUDIO_EN;
	else {
		if(!(config & CF_AUDIO_EN)) /* enable headphone mode */
			config |= CF_AUDIO_EN;
		else if(!(config & CF_CALLIOPE_SPKR_EN)) /* enable speaker mode */
			config |= CF_CALLIOPE_SPKR_EN;
		else /* silence */
			config &= ~(CF_CALLIOPE_SPKR_EN | CF_AUDIO_EN);
		audio_reconfigure();
	} /* calliope mode */

	if(config & CF_AUDIO_EN)
		audio_signal();
};

/* visualisation modes:
 * 0: persistence with fadeout from RSSI				[DEFAULT]
 * 1: blink with RSSI brightness
 * 2: persistence at full brightness
 * 3: blink at full brightness
 */
static void visual_mode_change(uint8_t inc) {
	static uint8_t mode = 0;
	
	mode+=inc;
	mode&=3;

	/* fadeout? */
	if(!mode)
		config |= CF_FADEOUT_EN;
	else
		config &= ~CF_FADEOUT_EN;
	
	/* short blinks vs. inactive after 2 seconds */
	rpi_age_fadeout = bd_age_fadeout = (mode&1) ? 5 : BD_AGE_TIMEOUT;
	if(config & CF_ALLBLE_EN) {
		rpi_age_fadeout = (mode&1) ? 2 : BD_AGE_TIMEOUT;
		bd_age_fadeout = (mode&1) ? 7 : BD_AGE_TIMEOUT;
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
 * A short click : audio clicks on/off
 * B short click : change visualisation mode
 *
 * A long click  : enable RPI output via USB serial
 * B long click  : switch between exposure notifications only and all BLE devices
 *
 * A during reset: output unfiltered raw beacon data via serial interface
 * B during reset: sequential LED usage instead of randomized
 */
void onLongClick(int btn_id) {
	if (btn_id == MICROBIT_ID_BUTTON_A)
		config ^= CF_UART_EN;
	else if (btn_id == MICROBIT_ID_BUTTON_B) {
		config ^= CF_ALLBLE_EN;
		visual_mode_change(0);
	}
}

void onClick(int btn_id) {
	if (btn_id == MICROBIT_ID_BUTTON_A)
		audio_mode_change();
    else if (btn_id == MICROBIT_ID_BUTTON_B)
		visual_mode_change(1);
}

static void button_service(void) {
	static uint8_t btn_a_count = 0, btn_b_count = 0;

	if(BTN_A_PRESSED())
		btn_a_count += btn_a_count < 255 ? 1 : 0;
	else if(btn_a_count) {
		if(btn_a_count >= BTN_LONGCLICK_THRESHOLD)
			onLongClick(MICROBIT_ID_BUTTON_A);
		else if(btn_a_count >= BTN_CLICK_THRESHOLD)
			onClick(MICROBIT_ID_BUTTON_A);
		btn_a_count = 0;
	}

	if(BTN_B_PRESSED())
		btn_b_count += btn_b_count < 255 ? 1 : 0;
	else if(btn_b_count) {
		if(btn_b_count >= BTN_LONGCLICK_THRESHOLD)
			onLongClick(MICROBIT_ID_BUTTON_B);
		else if(btn_b_count >= BTN_CLICK_THRESHOLD)
			onClick(MICROBIT_ID_BUTTON_B);
		btn_b_count = 0;
	}
}

static void randomize_age(void) {
	uint32_t set=(1<<BD_N)-1;
	uBit.seedRandom();
	while(set) {
		uint16_t v = uBit.random(BD_N);
		if(set&(1<<v)) {
			set &= ~(1<<v);
			seen((uint8_t*)&v, INT8_MIN, 0); /* short_ids must be preinitialized to 0..BD_N or read from NULL ptr will be triggered! */
		}
	}
}

/* TODO: handle rollover
 * case 1: end & ts1 pre-rollover              : no problem
 * case 2: end & ts1 post-rollover             : no problem
 * case 3: end post-rollover, ts1 pre-rollover : ts1 > end
 * case 4: end pre-rollover , ts1 post-rollover: ts1 < end
 */
static uint32_t wait_until(uint32_t end) {
	uint32_t ts1 = uBit.systemTime(), ts2;
	if(ts1 >= end)
		return ts1;
	uBit.sleep(end-ts1);
	ts2 = uBit.systemTime();
	/*
	char buf[32];
	sprintf(buf,"wait %ld\r\n",end-ts1);
	uBit.serial.send(buf,ASYNC);
	*/
	return ts2;
}

/* micro:bit:
 *   BTN A: P17 low active
 *   BTN B: P26 low active
 *   SCL: P0.00 (pad 4)
 *   SDA: P0.30 (pad 3)
 *   MAG3110: mag - 0x0E<<1
 *   MMA8653FC: accel - 0x3B
 *   LSM303AGR: mag+accel - Linear acceleration sensor: 0x33, Magnetic field sensor: 0x3d
 *   P0: P0.03  ==> audio output
 *   P1: P0.02
 *   P2: P0.01
 *
 * Calliope Mini:
 *   BTN A: P17 low active
 *   BTN B: P16 low active (P16 at microbit edge connector)
 *   SCL: P0.19 (pad 27)
 *   SDA: P0.20 (pad 28)
 *   BMX055: mag+gyro+accel - accel: 0x18<<1, magn: 0x10<<1, gyro: 0x68<<1
 *   DRV8837:
 *      nSLEEP: P28 (sleep if low) - MMA8653FC_INT1 on microbit
 *      IN1: P29 - MAG31100INT1 on microbit
 *      IN2: P30 - SDA on microbit
 *   P0: P0.00 (SCL on microbit)
 *   P1: P0.01 (P2 on microbit)  ==> audio output
 *   P2: P0.02 (P1 on microbit)
 *   P3: P0.22 (MISO on microbit)
 */

static void hw_detect(void) {
	char tmp;
	int res = uBit.i2c.read(0x33, &tmp, 1);             /* try to read from micro:bit LSM303AGR */
	if(res == MICROBIT_OK)
		return;
	res = uBit.i2c.read(0x3b, &tmp, 1);                 /* if LSM303AGR not found try read from MMA8653FC */
	if(res == MICROBIT_OK)
		return;
	else {                                              /* doesn't look like a micro:bit */
		MicroBitI2C* calliope_i2c = new MicroBitI2C(P0_20, P0_19);             /* try Calliope mini I2C */
		res = calliope_i2c->read(0x18<<1, &tmp, 1);     /* try to read from calliope BMX055 accel */
		if(res != MICROBIT_OK) {                        /* this shouldn't happen oO - not a calliope either? */
			delete calliope_i2c;
			return;
		}
		/* Calliope mini detected */
		config |= CF_HW_CALLIOPE;
		i2c_bus = calliope_i2c;
		uBit.io.P16.setPull(PullNone);         /* make button B usable on Calliope */
	}
}

/* TODO:
 * 
 * - handle uBit.systemTime() overflow
 * 
 * further thoughts:
 * - better parser for advertisement data?
 * - serial: support serial commands? (e.g. RPI-to-UART en/disable?)
 * - audio: mute clicks from oldest RPI or RPI with highest seen counter instead of highest RSSI?
 * - visual: map age/seen counter to LED position?
 * - visual: stretch fadeout from RSSI to zero in RSSI-mode?
 */
int main() {
	uint32_t now = uBit.systemTime();
	uint32_t last_cntprint = 0;
	uint8_t bds_active, rpis_active;
	uint8_t audio_done = 0, sleep_time = REFRESH_DELAY;

	hw_detect();

	uBit.serial.setTxBufferSize(UART_TXBUFSZ);

	bd_list_init();

	if(!BTN_A_PRESSED())
		randomize_age();

	if(BTN_B_PRESSED())
		config |= CF_RPIS_DONTBLINK;

	visual_mode_change(0);

	/* display project identifier and version string both via LEDs and USB serial */
	uBit.display.scroll("cs-" VERSION_STRING, MICROBIT_DEFAULT_SCROLL_SPEED/2);
	uBit.serial.send("corona-scanner " VERSION_STRING "\r\n", SYNC_SPINWAIT);

	/* show detected hardware platform */
	uBit.serial.send("hardware: ", SYNC_SPINWAIT);
	uBit.serial.send(config & CF_HW_CALLIOPE ? "Calliope mini\r\n" : "micro:bit\r\n", SYNC_SPINWAIT);

	if(is31fl3738_init() == MICROBIT_OK) {
		uBit.serial.send("using IS31FL3738 output\r\n", SYNC_SPINWAIT);
		config |= CF_EXTLEDS_EN;
	}

	btle_set_gatt_table_size(BLE_GATTS_ATTR_TAB_SIZE_MIN);

    uBit.ble = new BLEDevice();
    uBit.ble->init();

    uBit.ble->gap().setScanParams(500, 400);
    uBit.ble->gap().startScan(advertisementCallback);

	audio_init();

    while (true) {
		now = wait_until(now + sleep_time);

		audio_off();

		button_service();

		bds_active = refresh_screen(now, &rpis_active);

		/* prevent inaccurate huge seen counter readings if more than BD_N active BDs are seen */
		if(bds_active == BD_N)
			thrashing_likely = THRASHING_LOCKOUT;
		else if (thrashing_likely)
			thrashing_likely--;

		/* output bd counter every ~8 seconds */
		if((last_cntprint != (now>>13)) && (UART_CANQUEUE(84))) {
			char buf[70];
			last_cntprint = now>>13;
			if(!(config & CF_ALLBLE_EN))
				sprintf(buf,"RPIs active: %s%2d seen: %" PRIu32 "\r\n", thrashing_likely ? ">=" : "",  rpis_active, rpis_seen);
			else {
				sprintf(buf,"BDs active: %s%2d (RPIs: %s%2d) seen: %" PRIu32 " (RPIs: %" PRIu32 ")\r\n",
					thrashing_likely ? ">=" : "",  bds_active,
					thrashing_likely ? ">=" : "",  rpis_active,
					bds_seen, rpis_seen);
			}
			uBit.serial.send(buf, ASYNC);
		}

		/* generate audio signal if enabled */
		sleep_time = REFRESH_DELAY;
		if(audio_done != audio_request) {
			audio_done = audio_request;
			if(config & CF_AUDIO_EN)
				sleep_time -= audio_signal();
		}
    } /* main loop */
    return 0;
}
