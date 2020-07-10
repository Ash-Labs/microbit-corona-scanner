/*
The MIT License (MIT)

This code is based on CalliopeSoundMotor.cpp:

Copyright (c) 2016 Calliope GbR
This software is provided by DELTA Systems (Georg Sommer) - Thomas Kern 
und Björn Eberhardt GbR by arrangement with Calliope GbR. 

other code parts Copyright (c) 2020 hunz <Zn000h@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include <stdint.h>

#include "MicroBitCustomConfig.h"
#include "MicroBit.h"

#include "MicroBitSystemTimer.h"
#include "nrf_gpiote.h"
#include "nrf_gpio.h"

#include "config_flags.h"

#define MICROBIT_PIN_HP_OUTPUT             p3
#define CALLIOPE_PIN_HP_OUTPUT             p1
#define CALLIOPE_PIN_SPKR_SLEEP            p28
#define CALLIOPE_PIN_SPKR_IN1              p29
#define CALLIOPE_PIN_SPKR_IN2              p30

//constants
#define CALLIOPE_SM_DEFAULT_DUTY_S                      100
#define CALLIOPE_SM_PRESCALER_S                         0
#define CALLIOPE_SM_PRESCALER_S_LF                      4           //prescaler for creating low frequencies
#define CALLIOPE_MIN_FREQUENCY_HZ_S_NP                  245         //min possible frequency due to 16bit timer resolution (without prescaler)
#define CALLIOPE_MIN_FREQUENCY_HZ_S                     20          //min human audible frequency
#define CALLIOPE_MAX_FREQUENCY_HZ_S                     20000       //max human audible frequency 
#define CALLIOPE_BOARD_FREQUENCY                        16000000

extern MicroBit uBit;
extern uint16_t config;

static uint8_t  audio_active                = 0;
static uint32_t audio_output1               = MICROBIT_PIN_HP_OUTPUT;

void audio_off(void) {
	
	if(!audio_active)
		return;

	audio_active = 0;

    //stop & clear timer 
    NRF_TIMER2->TASKS_STOP = 1;
    NRF_TIMER2->TASKS_CLEAR = 1;

	//clear pins
	nrf_gpio_pin_clear(audio_output1);

    //disable GPIOTE control of the pins
    nrf_gpiote_task_disable(0);

    if(config & CF_CALLIOPE_SPKR_EN) {
		nrf_gpiote_task_disable(1);
		nrf_gpio_pin_clear(CALLIOPE_PIN_SPKR_IN2);
		nrf_gpio_pin_clear(CALLIOPE_PIN_SPKR_SLEEP);
	}
}

int audio_signal(void) {

	audio_active = 1;

	nrf_gpio_pin_clear(audio_output1);

	/* continous tone is too loud for headphones
	 * therefor we only generate a continous tone for speaker
	 * and a single 'click' using {on, 1ms delay, off} for headphones */
	if(config & CF_CALLIOPE_SPKR_EN) {
		nrf_gpio_pin_set(CALLIOPE_PIN_SPKR_IN2);
		nrf_gpio_pin_set(CALLIOPE_PIN_SPKR_SLEEP);
		nrf_gpiote_task_enable(1);
	}

	nrf_gpiote_task_enable(0);
	NRF_TIMER2->TASKS_START = 1;

	if(config & CF_CALLIOPE_SPKR_EN)
		return 0;

	/* generate click instead of continous tone */
	uBit.sleep(1);
	audio_off();
	return 1;
}

static void timer_config(uint32_t hz) {
	//max 50% duty per pwm just like in dual motor use
    uint8_t duty = uint8_t(CALLIOPE_SM_DEFAULT_DUTY_S/2);

    //calculate period corresponding to the desired frequency and the currently used prescaler
    uint16_t period = hz < CALLIOPE_MIN_FREQUENCY_HZ_S_NP ?
		uint16_t(uint32_t(CALLIOPE_BOARD_FREQUENCY) / (uint32_t(hz) << CALLIOPE_SM_PRESCALER_S_LF)) :
		uint16_t(uint32_t(CALLIOPE_BOARD_FREQUENCY) / uint32_t(hz));

    NRF_TIMER2->PRESCALER = hz < CALLIOPE_MIN_FREQUENCY_HZ_S_NP ? CALLIOPE_SM_PRESCALER_S_LF : CALLIOPE_SM_PRESCALER_S;

    //set compare register 2 and 3 according to the gives frequency (this sets the PWM period)
    NRF_TIMER2->CC[2] = period-1;
    NRF_TIMER2->CC[3] = period;

    //set duty cycle
    NRF_TIMER2->CC[0] = period - uint16_t(uint32_t((period * duty) / 100));
    NRF_TIMER2->CC[1] = uint16_t(uint32_t((period * duty) / 100)) - 1;
}

void audio_reconfigure(void) {
	if(!(config & CF_AUDIO_EN))
		return;
	audio_output1 = config & CF_CALLIOPE_SPKR_EN ? CALLIOPE_PIN_SPKR_IN1 : CALLIOPE_PIN_HP_OUTPUT;
	nrf_gpiote_task_configure(0, audio_output1, NRF_GPIOTE_POLARITY_TOGGLE, NRF_GPIOTE_INITIAL_VALUE_LOW);
	timer_config(config & CF_CALLIOPE_SPKR_EN ? 4000 : 1000); /* 1kHz is a bit softer on the ears via headphones... */
}

void audio_init(void) {

	NRF_GPIOTE->POWER = 1;

    //init pins
    if(config & CF_HW_CALLIOPE) {
		audio_output1 = CALLIOPE_PIN_HP_OUTPUT;
		nrf_gpio_cfg_output(CALLIOPE_PIN_SPKR_SLEEP);
		nrf_gpio_pin_clear(CALLIOPE_PIN_SPKR_SLEEP);
		nrf_gpio_cfg_output(CALLIOPE_PIN_SPKR_IN1);
		nrf_gpio_cfg_output(CALLIOPE_PIN_SPKR_IN2);
		nrf_gpiote_task_configure(1, CALLIOPE_PIN_SPKR_IN2, NRF_GPIOTE_POLARITY_TOGGLE, NRF_GPIOTE_INITIAL_VALUE_HIGH);
	}
	
	nrf_gpio_cfg_output(audio_output1);
	nrf_gpio_pin_clear(audio_output1);
	
	nrf_gpiote_task_configure(0, audio_output1, NRF_GPIOTE_POLARITY_TOGGLE, NRF_GPIOTE_INITIAL_VALUE_LOW);

    //Three NOPs are required to make sure configuration is written before setting tasks or getting events
    __NOP();
    __NOP();
    __NOP();
    
    //connect the tasks to the corresponding compare match events, toggle twice per period (PWM)
    //connect task 0
    NRF_PPI->CH[0].EEP = (uint32_t)&NRF_TIMER2->EVENTS_COMPARE[0];
    NRF_PPI->CH[0].TEP = (uint32_t)&NRF_GPIOTE->TASKS_OUT[0];
    //connect task 1
    NRF_PPI->CH[1].EEP = (uint32_t)&NRF_TIMER2->EVENTS_COMPARE[1];
    NRF_PPI->CH[1].TEP = (uint32_t)&NRF_GPIOTE->TASKS_OUT[1];

    //connect task 0
    NRF_PPI->CH[2].EEP = (uint32_t)&NRF_TIMER2->EVENTS_COMPARE[2];
    NRF_PPI->CH[2].TEP = (uint32_t)&NRF_GPIOTE->TASKS_OUT[0];
    //connect task 1
    NRF_PPI->CH[3].EEP = (uint32_t)&NRF_TIMER2->EVENTS_COMPARE[3];
    NRF_PPI->CH[3].TEP = (uint32_t)&NRF_GPIOTE->TASKS_OUT[1];

    NRF_PPI->CHENSET = 15; // bits 0 - 3 for channels 0 - 3

     //init TIMER2 for PWM use
    NRF_TIMER2->POWER = 1;
    NRF_TIMER2->MODE = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;
    NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_16Bit << TIMER_BITMODE_BITMODE_Pos;
	NRF_TIMER2->SHORTS = TIMER_SHORTS_COMPARE3_CLEAR_Msk;
	
    //stop & clear timer 
    NRF_TIMER2->TASKS_STOP = 1;
    NRF_TIMER2->TASKS_CLEAR = 1;
    
	timer_config(1000);
}
