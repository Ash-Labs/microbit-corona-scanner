#include "MicroBitCustomConfig.h"
#include "MicroBit.h"

#include "is31fl3738.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

//#define I2C_DEBUG

extern MicroBit uBit;

#ifdef I2C_DEBUG
#define I2C_WRITE i2c_write_debug
static int i2c_write_debug(uint8_t addr, const char* buf, int n) {
	int i;
	uBit.serial.printf("write %d: ",n);
	for(i=0;i<n;i++)
		uBit.serial.printf("%02x ",buf[i]);
	i = uBit.i2c.write(addr, buf, n);
	uBit.serial.printf("res %d\r\n",i);
	return i;
}
#else
#define I2C_WRITE uBit.i2c.write
#endif

#define CMD(c)         ((c)|0x80)
#define DATA(n)        (n)
#define INIT_END       0

static const uint8_t init_data[] __attribute__ ((aligned (4))) = {
	//CMD(IS31FL3738_CMD_LEDCTL), DATA(4), 0x00, 0x03, 0x00, 0x03,      /* turn LED 1A on */
	CMD(IS31FL3738_CMD_LEDCTL), DATA(21), 0x00,                      /* turn 5x5 LEDs on */
        0xff, 0x03, 0xff, 0x03,  /* SW1 */
        0xff, 0x03, 0xff, 0x03,  /* SW2 */ 
        0xff, 0x03, 0xff, 0x03,  /* SW3 */
        0xff, 0x03, 0xff, 0x03,  /* SW4 */
        0xff, 0x03, 0xff, 0x03,  /* SW5 */
	CMD(IS31FL3738_CMD_FUNC),   DATA(3), 0x00, 0x01, 0x80,            /* normal operation, set GCRR to 128 */
	CMD(IS31FL3738_CMD_PWM),                                          /* switch to PWM page access */
	//DATA(3), 0x00, 0x40, 0x40, /* PWM test data */
	INIT_END
};

#define SLAVE_ADDR	(0x50<<1)

static int is31fl3738_cmd(uint8_t cmd) {
	uint8_t buf[2] = {IS31FL3738_REG_WRLOCK, IS31FL3738_WRLOCK_MAGIC};
	int res = I2C_WRITE(SLAVE_ADDR, (char*)buf, 2);
	if(res != MICROBIT_OK)
		return res;
	buf[0] = IS31FL3738_REG_CMD;
	buf[1] = cmd;
	return I2C_WRITE(SLAVE_ADDR, (char*)buf, 2);
}

#define CS_N        8
#define SW_N        5
#define PWM_REGS	(CS_N*2*SW_N*2)

static uint8_t pwm_cache[PWM_REGS+1];     /* reserve 1 additional byte for the i2c address byte */

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
			res = I2C_WRITE(SLAVE_ADDR, (const char *)init+1, v);
			init+=v;
		}
		if(res != MICROBIT_OK)
			return res;
	}

	memset(pwm_cache, 0, sizeof(pwm_cache));

	return MICROBIT_OK;
}

void is31fl3738_update(void) {
	int res = I2C_WRITE(SLAVE_ADDR, (char*)pwm_cache, sizeof(pwm_cache));   /* send 1 additional byte (address) */
//	if(res != MICROBIT_OK)
//		uBit.serial.printf("i2c %d\n",res);
}

void is31fl3738_setPixel(int16_t x , int16_t y, uint8_t value) {
	uint8_t idx = y*32 + x*2 + 1; /* use offset +1 to have some space for the address byte during i2c write */

	if((x>4)||(y>4))
		return;

	pwm_cache[idx++] = value;
	pwm_cache[idx++] = value;
	idx+=14;
	pwm_cache[idx++] = value;
	pwm_cache[idx]   = value;
}
