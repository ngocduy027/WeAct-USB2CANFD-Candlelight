/* Reset into the STM32G4 ROM bootloader without programming option bytes. */
#include "dfu.h"
#include "system.h"
#include "can.h"
#include "usb_core.h"

#define DFU_MAGIC 0xB00710ADU
#define G4_SYSTEM_MEMORY 0x1FFF0000U
static volatile uint32_t dfu_magic[2] __attribute__((section(".noinit")));
static volatile bool dfu_requested;
static uint32_t dfu_requested_at;

__attribute__((naked, noreturn)) static void dfu_jump(uint32_t stack, uint32_t entry)
{
    __asm volatile (
        "msr msp, r0\n"
        "movs r0, #0\n"
        "msr control, r0\n"
        "msr basepri, r0\n"
        "msr faultmask, r0\n"
        "isb\n"
        "cpsie i\n"
        "bx r1\n"
    );
}

// Called directly from Reset_Handler, before .data/.bss initialization or SystemInit.
void dfu_check_early(void)
{
    bool requested = dfu_magic[0] == DFU_MAGIC && dfu_magic[1] == ~DFU_MAGIC;
    dfu_magic[0] = 0;
    dfu_magic[1] = 0;
    if (!requested) return;
    const volatile uint32_t *vectors = (const volatile uint32_t *)G4_SYSTEM_MEMORY;
    uint32_t stack = vectors[0];
    uint32_t entry = vectors[1];
    if (stack < 0x20000000U || stack > 0x20080000U || (stack & 7U) ||
        !(entry & 1U) || entry < G4_SYSTEM_MEMORY || entry >= 0x20000000U) return;
    SCB->VTOR = G4_SYSTEM_MEMORY;
    __DSB();
    __ISB();
    dfu_jump(stack, entry);
}

eFeedback dfu_switch_to_bootloader(void)
{
    if (system_get_mcu_serie() != SERIE_G4) return FBK_UnsupportedFeature;
    dfu_requested_at = HAL_GetTick();
    dfu_requested = true; // ISR-safe request; all shutdown happens in main.
    return FBK_Success;
}

void dfu_timer_100ms(uint32_t now)
{
    if (!dfu_requested || now - dfu_requested_at < 300U) return;
    dfu_requested = false;
    can_close_all();
    USBD_LL_Stop(&GLB_UsbDevice); // Drop the pull-up before the clean reset.
    dfu_magic[0] = DFU_MAGIC;
    dfu_magic[1] = ~DFU_MAGIC;
    __DSB();
    NVIC_SystemReset();
}
