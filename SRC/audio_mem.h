#ifndef SRC_AUDIO_MEM_H
#define SRC_AUDIO_MEM_H

#if defined(__CC_ARM) || defined(__ARMCC_VERSION) || defined(__GNUC__)
#define AUDIO_CCMRAM __attribute__((section(".CCMRAM"), aligned(4)))
#define AUDIO_SRAM1  __attribute__((section(".SRAM1"), aligned(4)))
#define AUDIO_SRAM2  __attribute__((section(".SRAM2"), aligned(4)))
#else
#define AUDIO_CCMRAM
#define AUDIO_SRAM1
#define AUDIO_SRAM2
#endif

#endif /* SRC_AUDIO_MEM_H */
