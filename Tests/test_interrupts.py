#!/usr/bin/env python3
"""Inactive FDCAN IRQ acknowledgement and build-specific servicing ownership."""
import unittest
from test_regressions import ROOT, function, run_c


class InterruptOwnership(unittest.TestCase):
    def test_inactive_or_error_session_acknowledges_all_enabled_sources(self):
        handler = function(ROOT / 'Source/can.c', 'void can_interrupt(int channel)')
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define HAL_FDCAN_STATE_BUSY 2
#define FDCAN_FLAG_TX_EVT_FIFO_ELT_LOST 1U
#define FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST 2U
#define FDCAN_FLAG_RX_FIFO1_MESSAGE_LOST 4U
#define FDCAN_IT_ERROR_WARNING 8U
#define FDCAN_IT_ERROR_PASSIVE 16U
#define FDCAN_IT_BUS_OFF 32U
#define FDCAN_IT_ARB_PROTOCOL_ERROR 64U
#define FDCAN_IT_DATA_PROTOCOL_ERROR 128U
typedef struct {uint32_t IR,IE;} peripheral;
typedef struct {peripheral *Instance;unsigned State;} FDCAN_HandleTypeDef;
typedef struct {bool is_open;FDCAN_HandleTypeDef handle;} can_class;
static can_class can_inst[1];static unsigned sampled,serviced;
static void can_sample_status(int channel){sampled++;}
static void can_service_io(int channel){serviced++;}
''' + handler + r'''
int main(void){
 peripheral registers={0};can_inst[0].handle.Instance=&registers;
 for(unsigned open=0;open<2;open++)for(unsigned state=0;state<4;state++){
  if(open && state==HAL_FDCAN_STATE_BUSY)continue;
  can_inst[0].is_open=open;can_inst[0].handle.State=state;
  uint32_t latched=0x1FFU;registers.IR=latched;registers.IE=0xFFU;sampled=serviced=0;
  can_interrupt(0);
  // MMIO IR is W1C: the C register fixture captures the written ack mask.
  uint32_t written=registers.IR;latched&=~written;
  assert(written==0xFFU && !(latched&registers.IE) && latched==0x100U);
  assert(!sampled && !serviced);
 }
 can_inst[0].is_open=true;can_inst[0].handle.State=HAL_FDCAN_STATE_BUSY;
 registers.IR=registers.IE=0xFFU;can_interrupt(0);
 assert(registers.IR==0xF8U && sampled==1 && serviced==1);
 return 0;
}
''')

    def test_only_candlelight_enables_fdcan_nvic_lines(self):
        init = function(ROOT / 'Source/system.c', 'void system_init_timestamp()\n{')
        constants = '\n'.join(line for line in (ROOT / 'Source/system.h').read_text().splitlines()
                              if line.startswith('#define SYSTEM_TIMESTAMP_TICK_US'))
        for candlelight in (False, True):
            run_c(('#define Candlelight 1\n' if candlelight else '') + constants + r'''
#include <stdint.h>
#include <assert.h>
#define CHANNEL_COUNT 3
#define TIM_DIER_UIE 1U
#define TIM_EGR_UG 1U
#define TIM_CR1_CEN 1U
#define TIM3_IRQn 0
#define FDCAN1_IT0_IRQn 1
#define FDCAN2_IT0_IRQn 2
#define FDCAN3_IT0_IRQn 3
#define __HAL_RCC_TIM3_CLK_ENABLE() ((void)0)
static uint32_t SystemCoreClock=160000000,timestamp_wrap;
static struct {uint32_t CR1,CR2,SMCR,DIER,CCMR1,CCMR2,CCER,PSC,ARR,EGR,SR;} timer;
#define TIM3 (&timer)
static unsigned enabled;
static void HAL_NVIC_SetPriority(unsigned irq,unsigned priority,unsigned sub){assert(irq?priority==1:priority==0);}
static void HAL_NVIC_EnableIRQ(unsigned irq){enabled|=1U<<irq;}
''' + init + r'''
int main(void){
 system_init_timestamp();assert(timer.PSC==159 && timer.ARR==65535 && enabled&1);
#ifdef Candlelight
 assert(enabled==15);
#else
 assert(enabled==1);
#endif
 return 0;
}
''')


if __name__ == '__main__':unittest.main()
