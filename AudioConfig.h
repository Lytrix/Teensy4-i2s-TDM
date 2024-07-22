#pragma once

#include <stdint.h>

#define CHANNELS 4
#define SAMPLERATE 192000
#define BIT_DEPTH 32
#define AUDIO_BLOCK_SAMPLES 128

#define SAMPLE_16_MAX INT16_MAX
#define SAMPLE_16_MIN INT16_MIN
#define SAMPLE_24_MAX 8388607 // 24 bit signed max
#define SAMPLE_24_MIN -8388608 // 24 bit signed min
#define SAMPLE_32_MAX INT32_MAX
#define SAMPLE_32_MIN INT32_MIN

#define AudioNoInterrupts() (NVIC_DISABLE_IRQ(IRQ_SOFTWARE))
#define AudioInterrupts()   (NVIC_ENABLE_IRQ(IRQ_SOFTWARE))
