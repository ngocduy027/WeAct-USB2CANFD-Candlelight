#include "system.h"

#define FAULT_COOKIE 0xFA017EADU
static volatile struct {
    uint32_t cookie, inverse, reason;
} retained_fault __attribute__((section(".noinit")));
static IWDG_HandleTypeDef watchdog;
static uint32_t last_reset_reason;

void system_runtime_init(void)
{
    if (retained_fault.cookie == FAULT_COOKIE && retained_fault.inverse == ~FAULT_COOKIE)
        last_reset_reason = retained_fault.reason;
    else if (RCC->CSR & RCC_CSR_IWDGRSTF)
        last_reset_reason = SYSTEM_FAULT_WATCHDOG;
    retained_fault.cookie = 0;
    retained_fault.inverse = 0;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    watchdog.Instance = IWDG;
    watchdog.Init.Prescaler = IWDG_PRESCALER_256;
    watchdog.Init.Reload = 249; // Approximately two seconds at the nominal 32 kHz LSI.
    watchdog.Init.Window = IWDG_WINDOW_DISABLE;
    if (HAL_IWDG_Init(&watchdog) != HAL_OK) system_fault_reset(SYSTEM_FAULT_WATCHDOG_INIT);
}

void system_watchdog_feed(void)
{
    // Only main calls this. IRQ activity cannot hide a stuck main loop.
    __HAL_IWDG_RELOAD_COUNTER(&watchdog);
}

uint32_t system_reset_reason(void) { return last_reset_reason; }

__attribute__((noreturn)) void system_fault_reset(uint32_t reason)
{
    retained_fault.reason = reason;
    retained_fault.cookie = FAULT_COOKIE;
    retained_fault.inverse = ~FAULT_COOKIE;
    __DSB();
    NVIC_SystemReset();
}
