#ifndef APP_MEMORY_H
#define APP_MEMORY_H

#include <stdint.h>

#define APP_CCMRAM_BASE   (0x10000000UL)
#define APP_CCMRAM_SIZE   (0x00010000UL)
#define APP_SRAM1_BASE    (0x20000000UL)
#define APP_SRAM1_SIZE    (0x0001C000UL)
#define APP_SRAM2_BASE    (0x2001C000UL)
#define APP_SRAM2_SIZE    (0x00004000UL)

#define APP_PTR_IN_CCMRAM(p) \
    ((((uintptr_t)(p)) >= APP_CCMRAM_BASE) && (((uintptr_t)(p)) < (APP_CCMRAM_BASE + APP_CCMRAM_SIZE)))

#define APP_PTR_IN_DMA_RAM(p) \
    (((((uintptr_t)(p)) >= APP_SRAM1_BASE) && (((uintptr_t)(p)) < (APP_SRAM1_BASE + APP_SRAM1_SIZE))) || \
     ((((uintptr_t)(p)) >= APP_SRAM2_BASE) && (((uintptr_t)(p)) < (APP_SRAM2_BASE + APP_SRAM2_SIZE))))

#if defined(__CC_ARM)
    #define APP_CCMRAM  __attribute__((section("CCMRAM"), zero_init, aligned(4)))
    #define APP_SRAM1   __attribute__((section("SRAM1"), zero_init, aligned(4)))
    #define APP_SRAM2   __attribute__((section("SRAM2"), zero_init, aligned(4)))
    #define APP_DMA_RAM __attribute__((section("DMA_RAM"), zero_init, aligned(4)))
#elif defined(__ARMCC_VERSION)
    #define APP_CCMRAM  __attribute__((section(".bss.ccmram"), aligned(4)))
    #define APP_SRAM1   __attribute__((section(".bss.sram1"), aligned(4)))
    #define APP_SRAM2   __attribute__((section(".bss.sram2"), aligned(4)))
    #define APP_DMA_RAM __attribute__((section(".bss.dma_ram"), aligned(4)))
#elif defined(__GNUC__)
    #define APP_CCMRAM  __attribute__((section(".bss.ccmram"), aligned(4)))
    #define APP_SRAM1   __attribute__((section(".bss.sram1"), aligned(4)))
    #define APP_SRAM2   __attribute__((section(".bss.sram2"), aligned(4)))
    #define APP_DMA_RAM __attribute__((section(".bss.dma_ram"), aligned(4)))
#else
    #define APP_CCMRAM
    #define APP_SRAM1
    #define APP_SRAM2
    #define APP_DMA_RAM
#endif

#endif /* APP_MEMORY_H */
