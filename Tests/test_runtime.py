#!/usr/bin/env python3
"""Check deferred DFU reset entry and retained-fault/watchdog ownership."""
import unittest
from test_regressions import ROOT, function, run_c


class RuntimeRecovery(unittest.TestCase):
    def test_dfu_defer_reset_and_rom_vectors(self):
        source = ROOT / 'Source/dfu.c'
        defs = '\n'.join(function(source, sig) for sig in [
            'void dfu_check_early(void)', 'eFeedback dfu_switch_to_bootloader(void)',
            'void dfu_timer_100ms(uint32_t now)'])
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <setjmp.h>
#include <sys/mman.h>
#define G4_SYSTEM_MEMORY 0x1FFF0000U
#define DFU_MAGIC 0xB00710ADU
#define SERIE_G4 4
#define FBK_Success 2
#define FBK_UnsupportedFeature 39
#define __DSB() ((void)0)
#define __ISB() ((void)0)
typedef int eFeedback;
static volatile uint32_t dfu_magic[2];
static volatile bool dfu_requested;static uint32_t dfu_requested_at;
static unsigned tick,serie=SERIE_G4,closed,usb_stopped,reset,jumped;
static unsigned GLB_UsbDevice;
static struct {uint32_t VTOR;} scb;
#define SCB (&scb)
static jmp_buf escape;
static uint32_t HAL_GetTick(void){return tick;}
static unsigned system_get_mcu_serie(void){return serie;}
static void can_close_all(void){closed++;}
static void USBD_LL_Stop(void *d){assert(d==&GLB_UsbDevice && closed);usb_stopped++;}
__attribute__((noreturn)) static void NVIC_SystemReset(void){assert(usb_stopped && dfu_magic[0]==DFU_MAGIC && dfu_magic[1]==~DFU_MAGIC);reset++;longjmp(escape,1);}
__attribute__((noreturn)) static void dfu_jump(uint32_t stack,uint32_t entry){assert(stack==0x20004000 && entry==0x1FFF1235);assert(!dfu_magic[0] && !dfu_magic[1]);jumped++;longjmp(escape,2);}
''' + defs + r'''
int main(void){
 serie=0;assert(dfu_switch_to_bootloader()==FBK_UnsupportedFeature && !dfu_requested);
 serie=SERIE_G4;tick=0xFFFFFFF0;assert(dfu_switch_to_bootloader()==FBK_Success && dfu_requested && !closed && !usb_stopped);
 dfu_timer_100ms(283);assert(!closed && !reset); // 299 ms across tick wrap.
 if(!setjmp(escape)){dfu_timer_100ms(284);assert(!"reset expected");}
 assert(reset==1 && !dfu_requested && closed==1);
 uint32_t *vectors=mmap((void *)G4_SYSTEM_MEMORY,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(vectors==(void *)G4_SYSTEM_MEMORY);vectors[0]=0x20004000;vectors[1]=0x1FFF1235;
 dfu_magic[1]^=1;dfu_check_early();assert(!jumped && !dfu_magic[0] && !dfu_magic[1]);
 for(unsigned invalid=0;invalid<3;invalid++){
  vectors[0]=invalid==0?0x10000000:0x20004000;vectors[1]=invalid==1?0x1FFF1234:invalid==2?0x08001235:0x1FFF1235;
  dfu_magic[0]=DFU_MAGIC;dfu_magic[1]=~DFU_MAGIC;dfu_check_early();assert(!jumped);
 }
 vectors[0]=0x20004000;vectors[1]=0x1FFF1235;dfu_magic[0]=DFU_MAGIC;dfu_magic[1]=~DFU_MAGIC;
 if(!setjmp(escape)){dfu_check_early();assert(!"ROM jump expected");}
 assert(jumped==1 && scb.VTOR==G4_SYSTEM_MEMORY);
 dfu_check_early();assert(jumped==1); // Retained request consumed only once.
 assert(!munmap(vectors,4096));return 0;
}
''')

    def test_retained_fault_and_watchdog_initialization(self):
        source = ROOT / 'Source/runtime.c'
        defs = '\n'.join(function(source, sig) for sig in [
            'void system_runtime_init(void)', 'void system_watchdog_feed(void)',
            'uint32_t system_reset_reason(void)',
            '__attribute__((noreturn)) void system_fault_reset(uint32_t reason)'])
        run_c(r'''
#include <stdint.h>
#include <assert.h>
#include <setjmp.h>
#define FAULT_COOKIE 0xFA017EADU
#define SYSTEM_FAULT_WATCHDOG 8
#define SYSTEM_FAULT_WATCHDOG_INIT 9
#define RCC_CSR_IWDGRSTF 1U
#define IWDG ((void *)0x40003000)
#define IWDG_PRESCALER_256 6
#define IWDG_WINDOW_DISABLE 4095
#define HAL_OK 0
#define __DSB() ((void)0)
static struct {uint32_t CSR;} rcc;
#define RCC (&rcc)
static volatile struct {uint32_t cookie,inverse,reason;} retained_fault;
typedef struct {void *Instance;struct {unsigned Prescaler,Reload,Window;} Init;} IWDG_HandleTypeDef;
static IWDG_HandleTypeDef watchdog;
static uint32_t last_reset_reason,feeds,fail_init,resets;
static jmp_buf escape;
#define __HAL_RCC_CLEAR_RESET_FLAGS() (rcc.CSR=0)
#define __HAL_IWDG_RELOAD_COUNTER(h) (feeds++)
static int HAL_IWDG_Init(IWDG_HandleTypeDef *h){assert(h->Instance==IWDG && h->Init.Prescaler==6 && h->Init.Reload==249 && h->Init.Window==4095);return fail_init;}
__attribute__((noreturn)) static void NVIC_SystemReset(void){resets++;longjmp(escape,1);}
__attribute__((noreturn)) void system_fault_reset(uint32_t reason);
''' + defs + r'''
int main(void){
 system_runtime_init();assert(!system_reset_reason() && !feeds && !retained_fault.cookie);
 system_watchdog_feed();assert(feeds==1);
 if(!setjmp(escape)){system_fault_reset(0x1234);assert(!"reset expected");}
 assert(resets==1 && retained_fault.cookie==FAULT_COOKIE && retained_fault.inverse==~FAULT_COOKIE);
 last_reset_reason=0;system_runtime_init();assert(system_reset_reason()==0x1234 && !retained_fault.cookie);
 last_reset_reason=0;retained_fault.cookie=FAULT_COOKIE;retained_fault.inverse=0;rcc.CSR=RCC_CSR_IWDGRSTF;
 system_runtime_init();assert(system_reset_reason()==SYSTEM_FAULT_WATCHDOG && !rcc.CSR);
 fail_init=1;if(!setjmp(escape)){system_runtime_init();assert(!"watchdog init failure must reset");}
 assert(resets==2 && retained_fault.reason==SYSTEM_FAULT_WATCHDOG_INIT);return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
