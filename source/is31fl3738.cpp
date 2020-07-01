#include "MicroBitCustomConfig.h"
#include "MicroBit.h"

#include "is31fl3738.h"

extern MicroBit uBit;

#define SLAVE_ADDR	(0x50<<1)

int is31fl3738_init(void) {
	char tmp;
	int res = uBit.i2c.read(SLAVE_ADDR, &tmp, 1);
	if(res != MICROBIT_OK)
		return res;

	uBit.i2c.frequency(400000);
	
	/* TODO: init */

	return MICROBIT_OK;
}

void is31fl3738_setPixel(int16_t x , int16_t y, uint8_t value) {
	
}
