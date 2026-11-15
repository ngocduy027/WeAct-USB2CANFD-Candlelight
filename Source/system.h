/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "settings.h"
#include "usb_def.h"

// The USB SETUP transfer is limited to 4096 bytes
// The SLCAN buffer also has it's limits.
// The user cannot transfer more than 2000 bytes (= 4000 hex digits for SLCAN)
// 2 bytes are subtracted from the flash page size for the unit16_t length stored before the flash data.
#define MAX_FLASH_DATA_LEN   MIN(2000, FLASH_PAGE_SIZE - 2)

typedef enum 
{
    OPT_BOR_Level4    = 1, // Set BoR Level4 (2.8 Volt)
    OPT_BOOT0_Enable  = 2, // Pin BOOT0 defines what is booted (flash or bootloader)
    OPT_BOOT0_Disable = 3, // Always boot into flash memory
} eOptionBytes;

typedef enum 
{
    SERIE_Unknown,
    SERIE_G0,      // STM32G0XX
    SERIE_G4,      // STM32G4XX
} eMcuSerie;

bool      system_init();
bool      system_is_option_enabled(eOptionBytes e_Option);
eFeedback system_set_option_bytes (eOptionBytes e_Option);
uint32_t  system_get_can_clock();
eMcuSerie system_get_mcu_serie();
uint32_t  system_get_timestamp();
uint32_t  system_get_timewrap();
// A 1 us capture tick spans 65.536 ms. Refuse bitrates whose conservative
// longest-frame bound would exceed that capture epoch. Wire units stay us.
#define SYSTEM_TIMESTAMP_TICK_US 1U
#define CAN_MIN_NOMINAL_BITRATE 5000U
#define CAN_MIN_NOMINAL_BITRATE_FD 20000U
uint32_t  system_extend_timestamp(uint16_t captured);
uint32_t  system_get_flash_addr(uint32_t segment);
eFeedback system_write_flash(uint32_t segment, uint8_t* buffer, uint16_t data_len);

// ARM's
// "Application Note 321 ARM Cortex-M Programming Guide to Memory Barrier Instructions"
// (from https://developer.arm.com/documentation/dai0321/latest) says that
// the ISBs are actually necessary on Cortex-M0 to avoid a 2-instruction
// delay in the effect of enabling and disabling interrupts.
// That probably doesn't matter here, but it's hard to say what the compiler
// will put in those 2 instructions so it's safer to leave it. The DSB isn't
// necessary on Cortex-M0, but it's architecturally required so we'll
// include it to be safe.
//
// The "memory" and "cc" clobbers tell GCC to avoid moving memory loads or
// stores across the instructions. This is important when an interrupt and the
// code calling disable_irq/enable_irq share memory. The fact that these are
// non-inlined functions probably forces GCC to flush everything to memory
// anyways, but trying to outsmart the compiler is a bad strategy (you never
// know when somebody will turn on LTO or something).

static inline void system_disable_irq()
{
	__disable_irq();
    __DSB(); // Data Synchronization Barrier
	__ISB(); // Instruction Synchronization Barrier
}

static inline void system_enable_irq()
{
	__enable_irq();
	__ISB(); // Instruction Synchronization Barrier
}

/* Scoped interrupt masks: restoring a nested lock must preserve its caller. */
static inline uint32_t system_irq_save(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    return mask;
}

static inline void system_irq_restore(uint32_t mask)
{
    __set_PRIMASK(mask);
    __ISB();
}

// ---------------------------------------

// Newlib syscall implementations live in syscalls.c with ABI-correct signatures.

enum {
    SYSTEM_FAULT_HARD = 1, SYSTEM_FAULT_MEMORY, SYSTEM_FAULT_BUS,
    SYSTEM_FAULT_USAGE, SYSTEM_FAULT_CLOCK, SYSTEM_FAULT_NMI,
    SYSTEM_FAULT_STARTUP, SYSTEM_FAULT_WATCHDOG, SYSTEM_FAULT_WATCHDOG_INIT,
    SYSTEM_FAULT_CAN_STOP
};
void system_runtime_init(void);
void system_watchdog_feed(void);
uint32_t system_reset_reason(void);
__attribute__((noreturn)) void system_fault_reset(uint32_t reason);
bool system_using_quartz(void);
