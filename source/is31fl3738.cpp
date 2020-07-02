#include "MicroBitCustomConfig.h"
#include "MicroBit.h"

#include "is31fl3738.h"

#define CMD(c)         ((c)|0x80)
#define DATA(n)        (n)
#define INIT_END       0

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

static const uint8_t init_data[] __attribute__ ((aligned (4))) = {
	CMD(IS31FL3738_CMD_LEDCTL), DATA(4), 0x00, 0x03, 0x00, 0x03,      /* turn LED 1A on */
	//CMD(IS31FL3738_CMD_LEDCTL), DATA(21), 0x00,                      /* turn 5x5 LEDs on */
    //    0xff, 0x03, 0xff, 0x03,  /* SW1 */
    //    0xff, 0x03, 0xff, 0x03,  /* SW2 */ 
    //    0xff, 0x03, 0xff, 0x03,  /* SW3 */
    //    0xff, 0x03, 0xff, 0x03,  /* SW4 */
    //    0xff, 0x03, 0xff, 0x03,  /* SW5 */
	CMD(IS31FL3738_CMD_FUNC),   DATA(3), 0x00, 0x01, 0xff,            /* normal operation, set GCRR to 255 */
	CMD(IS31FL3738_CMD_PWM),                                          /* switch to PWM page access */
	DATA(3), 0x00, 0x40, 0x40, /* PWM test data */
	INIT_END
};

extern MicroBit uBit;

#define SLAVE_ADDR	(0x50<<1)

static int i2c_write(uint8_t addr, const char* buf, int n) {
	int i;
#ifdef I2C_DEBUG
	for(i=0;i<n;i++)
		uBit.serial.printf("%02x ",buf[i]);
#endif
	i = uBit.i2c.write(addr, buf, n);
#ifdef I2C_DEBUG
	uBit.serial.printf("%d\r\n",i);
#endif
	return i;
}

static int is31fl3738_cmd(uint8_t cmd) {
	uint8_t buf[2] = {IS31FL3738_REG_WRLOCK, IS31FL3738_WRLOCK_MAGIC};
	int res = i2c_write(SLAVE_ADDR, (char*)buf, 2);
	if(res != MICROBIT_OK)
		return res;
	buf[0] = IS31FL3738_REG_CMD;
	buf[1] = cmd;
	return i2c_write(SLAVE_ADDR, (char*)buf, 2);
}

int is31fl3738_init(void) {
	const uint8_t *init;
	char tmp;
	int res = uBit.i2c.read(SLAVE_ADDR, &tmp, 1);
	if(res != MICROBIT_OK)
		return res;

	uBit.i2c.frequency(400000);

	for(init = init_data; *init; init++) {
		uint8_t v = *init;
		if(v&0x80)
			res = is31fl3738_cmd(v&0x7f);
		else {
			res = i2c_write(SLAVE_ADDR, (const char *)init+1, v);
			init+=v;
		}
		if(res != MICROBIT_OK)
			return res;
	}

	return MICROBIT_OK;
}

static uint8_t led_cache[25];
static uint8_t led_update_start = UINT8_MAX;
static uint8_t led_update_end   = 0;

void is31fl3738_update(void) {
	
	/* TODO */
	
	led_update_start = UINT8_MAX;
	led_update_end   = 0;
}

void is31fl3738_setPixel(int16_t x , int16_t y, uint8_t value, uint8_t draw_now) {
	uint8_t idx = x*5+y;
	
	led_cache[idx] = value;
	
	led_update_start = MIN(led_update_start, idx);
	led_update_end   = MAX(led_update_end,   idx);
	
	if(draw_now)
		is31fl3738_update();
}
