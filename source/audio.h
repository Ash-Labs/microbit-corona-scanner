
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

void audio_init(void);
int  audio_signal(void);
void audio_off(void);
void audio_reconfigure(void);

#endif /* AUDIO_H */
