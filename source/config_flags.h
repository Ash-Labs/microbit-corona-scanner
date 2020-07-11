#ifndef CONFIG_FLAGS_H
#define CONFIG_FLAGS_H

/* config bits */
#define CF_UART_EN					(1<<0)	 /* enable USB serial RPI output */
#define CF_ALLBLE_EN				(1<<1)   /* disable exposure notification filter */

#define CF_RSSI_BRIGHTNESS			(1<<2)	 /* use RSSI for LED brightness 	*/
#define CF_PERSISTENCE_EN			(1<<3)	 /* persistence visualisation 	*/
#define CF_FADEOUT_EN				(1<<4)	 /* fadeout LEDs over time 		*/
#define CF_EXTLEDS_EN				(1<<5)	 /* use external LEDs			*/

#define CF_AUDIO_EN					(1<<6)	 /* Audio output enable         */
#define CF_CALLIOPE_SPKR_EN         (1<<7)   /* Calliope mini speaker enable */

#define CF_HW_CALLIOPE				(1<<8)  /* Calliope mini hw detected   */

#endif /* CONFIG_FLAGS_H */
