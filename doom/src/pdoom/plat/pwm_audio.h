#ifndef SRC_PWM_AUDIO_H_
#define SRC_PWM_AUDIO_H_
#include "main.h"
#define MAX_CHANNELS 8
typedef struct { uint16_t lastAudioBufferIdx; uint16_t offset; uint8_t sfxIdx; int8_t volume; } soundChannel_t;
void initPwmAudio(void);
void updateSound(void);
void muteSound(void);
extern soundChannel_t soundChannels[MAX_CHANNELS];
#endif
