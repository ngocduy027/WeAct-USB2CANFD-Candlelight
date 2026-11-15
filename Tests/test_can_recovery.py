#!/usr/bin/env python3
"""Failure injection for real stop/recovery functions; MMIO and HAL are modeled."""
import unittest
from test_regressions import ROOT, function, run_c


class CanRecovery(unittest.TestCase):
    def test_error_handle_stop_and_recovery_intent(self):
        path = ROOT / 'Source/can.c'
        funcs = '\n'.join(function(path, sig) for sig in [
            'static void can_stop_hardware(int channel)\n{',
            'bool can_is_open(int channel)\n{',
            'bool can_is_any_open()\n{',
            'eFeedback can_is_tx_allowed(int channel)\n{',
            'void can_recover_bus_off(int channel)\n{',
        ])
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <setjmp.h>
#define Candlelight 1
#define CHANNEL_COUNT 2
#define HAL_OK 0
#define HAL_ERROR 1
#define HAL_FDCAN_STATE_READY 1
#define HAL_FDCAN_STATE_BUSY 2
#define HAL_FDCAN_STATE_ERROR 3
#define SYSTEM_FAULT_CAN_STOP 97
#define FDCAN_CCCR_INIT 1U
#define FDCAN_CCCR_CSR 2U
#define FDCAN_CCCR_CSA 4U
#define FDCAN_CCCR_CCE 8U
#define FDCAN_MODE_BUS_MONITORING 1
#define USR_ErrorReport 1U
#define FBK_Success 2
#define FBK_AdapterMustBeOpen 50
#define FBK_NoTxInSilentMode 51
#define FBK_BusIsOff 56
#define APP_CanTxFail 2
#define SET_BIT(reg,bits) ((reg)|=(deny_init && (bits)==FDCAN_CCCR_INIT ? 0U : (bits)))
#define CLEAR_BIT(reg,bits) ((reg)&=~(bits))
typedef int HAL_StatusTypeDef;
typedef int eFeedback;
typedef struct {uint32_t CCCR;} peripheral;
typedef struct {peripheral *Instance;unsigned State,LatestTxFifoQRequest;struct {unsigned Mode;} Init;} FDCAN_HandleTypeDef;
typedef struct {FDCAN_HandleTypeDef handle;bool is_open,suspended,recovery_needed,recover_bus_off,reinitializing;uint32_t recovery_last_tick,tx_pending;struct {unsigned BusOff;} cur_status;} can_class;
static can_class can_inst[2];static peripheral registers[2];
static uint32_t GLB_UserFlags[2];
static uint32_t tick,mask_state,fault;
static bool deny_init,stop_ok,start_ok,reopen_ok,busoff_pending;
static unsigned starts,retired,reopens;
static jmp_buf escape;
static uint32_t HAL_GetTick(void){return tick++;}
static uint32_t system_irq_save(void){uint32_t old=mask_state;mask_state=1;return old;}
static void system_irq_restore(uint32_t old){mask_state=old;}
__attribute__((noreturn)) static void system_fault_reset(uint32_t reason){fault=reason;longjmp(escape,1);}
static int HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h){assert(!mask_state);if(!stop_ok)return HAL_ERROR;h->Instance->CCCR|=FDCAN_CCCR_INIT|FDCAN_CCCR_CCE;h->State=HAL_FDCAN_STATE_READY;return HAL_OK;}
static int HAL_FDCAN_Start(FDCAN_HandleTypeDef *h){starts++;assert(mask_state);if(!start_ok)return HAL_ERROR;h->State=HAL_FDCAN_STATE_BUSY;return HAL_OK;}
static bool can_cancel_hardware(int channel){(void)channel;return true;}
static void HAL_FDCAN_DeactivateNotification(FDCAN_HandleTypeDef *h,uint32_t mask){(void)h;(void)mask;}
static void HAL_FDCAN_DeInit(FDCAN_HandleTypeDef *h){(void)h;}
static void error_assert(int channel,int flag,bool immediate){(void)channel;(void)flag;(void)immediate;}
static bool error_busoff_pending(int channel){(void)channel;return busoff_pending;}
static void buf_tx_cancel_all(int channel){(void)channel;retired++;}
static eFeedback can_open(int channel,unsigned mode){assert(can_inst[channel].reinitializing);reopens++;if(reopen_ok){can_inst[channel].is_open=true;return FBK_Success;}return HAL_ERROR;}
static bool can_is_open(int channel);
''' + funcs + r'''
int main(void){
 for(unsigned c=0;c<2;c++)can_inst[c].handle.Instance=&registers[c];
 can_inst[0].handle.State=HAL_FDCAN_STATE_ERROR;registers[0].CCCR=FDCAN_CCCR_CSR;
 can_stop_hardware(0);assert(can_inst[0].handle.State==HAL_FDCAN_STATE_READY);
 assert(registers[0].CCCR==(FDCAN_CCCR_INIT|FDCAN_CCCR_CCE));
 deny_init=true;registers[0].CCCR=0;tick=0xFFFFFFFA;
 if(!setjmp(escape)){can_stop_hardware(0);assert(!"unresponsive hardware must reset");}
 assert(fault==SYSTEM_FAULT_CAN_STOP && !mask_state);
 deny_init=false;fault=0;can_inst[0].suspended=true;
 assert(can_is_open(0) && can_is_any_open() && can_is_tx_allowed(0)==FBK_BusIsOff);
 can_inst[0].suspended=false;can_inst[0].recovery_needed=true;
 assert(can_is_open(0) && can_is_tx_allowed(0)==FBK_BusIsOff);
 start_ok=true;tick=100;can_recover_bus_off(0);
 assert(can_inst[0].is_open && !can_inst[0].recovery_needed && starts==1 && retired==1);
 assert(can_is_tx_allowed(0)==FBK_Success && !mask_state);
 can_inst[0].cur_status.BusOff=1;GLB_UserFlags[0]=USR_ErrorReport;busoff_pending=true;
 can_recover_bus_off(0);assert(starts==1); // Preserve a report until it has entered the host queue.
 busoff_pending=false;start_ok=false;tick=300;can_recover_bus_off(0);
 assert(!can_inst[0].is_open && can_inst[0].recovery_needed && reopens==1);
 assert(can_is_open(0) && can_is_tx_allowed(0)==FBK_BusIsOff);
 unsigned old_starts=starts;can_recover_bus_off(0);assert(starts==old_starts); // Retry deadline.
 reopen_ok=true;tick=500;can_recover_bus_off(0);
 assert(can_inst[0].is_open && !can_inst[0].recovery_needed && reopens==2 && !mask_state);
 // A last attempt older than half the millisecond range must not block recovery.
 can_inst[0]=(can_class){.handle={.Instance=&registers[0],.State=HAL_FDCAN_STATE_BUSY},.is_open=true,.cur_status={.BusOff=1}};
 GLB_UserFlags[0]=0;start_ok=true;unsigned before=starts;tick=0x80000100U;
 can_recover_bus_off(0);assert(starts==before+1 && can_inst[0].recovery_last_tick==0x80000100U);
 // Retry across the unsigned wrap: 80 ms is too soon, 100 ms is allowed.
 can_inst[0].is_open=false;can_inst[0].recovery_needed=true;can_inst[0].recover_bus_off=false;
 can_inst[0].recovery_last_tick=0xFFFFFFF0U;before=starts;tick=0x40U;
 can_recover_bus_off(0);assert(starts==before);
 tick=0x54U;can_recover_bus_off(0);assert(starts==before+1 && can_inst[0].recovery_last_tick==0x54U);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
