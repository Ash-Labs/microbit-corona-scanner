#include "MicroBitCustomConfig.h"
#include "MicroBit.h"

#include "is31fl3738.h"

/* example:
	# cmd write enable
	write 0xfe 0xC5
	# cmd write 
	write 0xfd 0x00 # write LED on/off

	# LED 1A on
	write 0x00 0x03
	write 0x02 0x03

	# cmd write enable
	write 0xfe 0xC5
	# cmd write 
	write 0xfd 0x03 # write function register

	# LED on
	write 0x00 0x01 # normal operation
	write 0x01 0xff # global current

	####

	# cmd write enable
	write 0xfe 0xC5
	# cmd write 
	write 0xfd 0x02 # write LED ABM

	# disable ABM for LED 1A
	write 0x00 0x00
	write 0x01 0x00
	write 0x10 0x00
	write 0x11 0x00

	####

	# cmd write enable
	write 0xfe 0xC5
	# cmd write 
	write 0xfd 0x01 # write LED PWM

	# PWM for LED 1A
	write 0x00 0x00
	write 0x01 0x00
	write 0x10 0x00
	write 0x11 0x00

	#exit

	# PWM for LED 1A
	write 0x00 0xff
	write 0x01 0xff
	write 0x10 0xff
	write 0x11 0xff

	exit

	# configure auto breath mode - because we can

	# cmd write enable
	write 0xfe 0xC5
	# cmd write 
	write 0xfd 0x03 # write function register

	write 0x00 0x01 # normal operation
	write 0x01 0xff # global current
	write 0x02 0x64 # T1 1.68S and T2 0.42S
	write 0x03 0x64 # T1 1.68S and T2 0.42S
	write 0x04 0x00 # begin T1 and end T2 and endless loop
	write 0x05 0x00 # endless loop
	write 0x0e 0x00 # up data
	write 0x00 0x03 # normal operation + auto breath

	# cmd write enable
	write 0xfe 0xC5
	# cmd write 
	write 0xfd 0x02 # write LED ABM

	# ABM for LED 1A
	write 0x00 0x01
	write 0x01 0x01
	write 0x10 0x01
	write 0x11 0x01
*/

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
