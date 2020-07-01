
#ifndef IS31FL3738_H
#define IS31FL3738_H

#include <stdint.h>

int is31fl3738_init(void);
void is31fl3738_setPixel(int16_t x , int16_t y, uint8_t value);

#endif
