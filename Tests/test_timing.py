#!/usr/bin/env python3
import unittest
from test_regressions import ROOT, function, run_c


class Timing(unittest.TestCase):
    def test_timestamp_wrap_and_pending_update(self):
        source = ROOT / 'Source/system.c'
        get = function(source, 'uint32_t system_get_timestamp(void)')
        extend = function(source, 'uint32_t system_extend_timestamp(')
        constants = '\n'.join(line for line in (ROOT / 'Source/system.h').read_text().splitlines() if line.startswith(('#define SYSTEM_TIMESTAMP_TICK_US', '#define CAN_MIN_NOMINAL_BITRATE')))
        run_c(constants + r'''
#include <stdint.h>
#include <assert.h>
#define TIM_SR_UIF 1U
static struct {uint32_t CNT,SR;} timer;
#define TIM3 (&timer)
static volatile uint32_t timestamp_wrap;
static uint32_t system_irq_save(void){return 0;}
static void system_irq_restore(uint32_t mask){(void)mask;}
''' + get + '\n' + extend + r'''
int main(void){
 timestamp_wrap=4;timer.CNT=3;timer.SR=0;
 assert(system_get_timestamp()==0x40003U*SYSTEM_TIMESTAMP_TICK_US);
 assert(system_extend_timestamp(65530)==0x3FFFAU*SYSTEM_TIMESTAMP_TICK_US);
 assert(system_extend_timestamp(2)==0x40002U*SYSTEM_TIMESTAMP_TICK_US);
 timer.SR=TIM_SR_UIF;assert(system_get_timestamp()==0x50003U*SYSTEM_TIMESTAMP_TICK_US);
 assert(system_extend_timestamp(65530)==0x4FFFAU*SYSTEM_TIMESTAMP_TICK_US);
  timestamp_wrap=65535;timer.SR=TIM_SR_UIF;assert(system_get_timestamp()==3);
 assert(system_extend_timestamp(65530)==0xFFFFFFFAU);
 // The allowed FD lower bound leaves room for a conservative 51.2 ms frame.
 timestamp_wrap=5;timer.CNT=30000;timer.SR=0;
 assert(system_get_timestamp()-system_extend_timestamp(44336)==51200);
 assert(SYSTEM_TIMESTAMP_TICK_US*65536U > 160ULL*1000000U/CAN_MIN_NOMINAL_BITRATE);
 assert(SYSTEM_TIMESTAMP_TICK_US*65536U > 1024ULL*1000000U/CAN_MIN_NOMINAL_BITRATE_FD);
 return 0;
}
''')

    def test_busload_uses_wide_math_and_actual_duration(self):
        timer = function(ROOT / 'Source/can.c', 'void can_timer_100ms()')
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define CHANNEL_COUNT 1
typedef struct {bool is_open;uint32_t busload_interval,busload_start_tick,bit_count_total,nom_bit_len_ns;uint8_t old_busload_pct;} can_class;
static can_class can_inst[1];
static uint32_t now;static unsigned calls,last_load,inject_bits;
static uint32_t system_irq_save(void){return 0;}
static void system_irq_restore(uint32_t mask){(void)mask;can_inst[0].bit_count_total+=inject_bits;inject_bits=0;}
static uint32_t HAL_GetTick(void){return now;}
static void control_report_busload(int channel,uint8_t load){(void)channel;calls++;last_load=load;}
''' + timer + r'''
int main(void){
 can_inst[0]=(can_class){.is_open=true,.busload_interval=100,.bit_count_total=2500000,.nom_bit_len_ns=2000};
 now=9999;can_timer_100ms();assert(!calls);
 inject_bits=37;now=10000;can_timer_100ms();assert(calls==1 && last_load==56);
 assert(can_inst[0].bit_count_total==37 && can_inst[0].busload_start_tick==10000);
 can_inst[0].bit_count_total=2500000;now=30000;can_timer_100ms();assert(last_load==28);
 can_inst[0]=(can_class){.is_open=true,.busload_interval=1,.busload_start_tick=0xFFFFFFF0,.bit_count_total=25000,.nom_bit_len_ns=2000};
 now=84;can_timer_100ms();assert(last_load==56);
 return 0;
}
''')


    def test_fd_mode_and_sjw_validation(self):
        can = ROOT / 'Source/can.c'
        header = (ROOT / 'STM32/STM32G4xx_HAL_Driver/Inc/stm32g4xx_hal_fdcan.h').read_text()
        macros = '\n'.join(line for line in header.splitlines() if line.startswith('#define IS_FDCAN_NOMINAL_') or line.startswith('#define IS_FDCAN_DATA_'))
        macros += '\n' + '\n'.join(line for line in (ROOT / 'Source/system.h').read_text().splitlines() if line.startswith('#define CAN_MIN_NOMINAL_BITRATE'))
        functions = '\n'.join([
            function(ROOT / 'Source/can.h', 'static inline uint32_t can_calc_baud('),
            function(can, 'eFeedback can_set_nom_bit_timing('),
            function(can, 'eFeedback can_set_data_bit_timing('),
            function(can, 'bool can_using_FD('),
            function(can, 'bool can_is_open('),
            function(can, 'eFeedback can_select_fd_mode('),
        ])
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define CAN_MAX_DATA_BITRATE 5000000U
#define FBK_Success 2
#define FBK_AdapterMustBeClosed 52
#define FBK_BaudrateNotSet 58
#define FBK_ParamOutOfRange 61
typedef int eFeedback;
typedef struct {uint32_t Brp,Seg1,Seg2,Sjw;} can_bitrate_cfg;
typedef struct {bool is_open,fd_enabled,suspended,recovery_needed;can_bitrate_cfg bitrate_nominal,bitrate_data;} can_class;
static can_class can_inst[1];
static bool can_is_open(int channel);
static uint32_t system_get_can_clock(void){return 160000000;}
''' + macros + '\n' + functions + r'''
int main(void){
 assert(can_set_nom_bit_timing(0,2,139,20,20)==FBK_Success);
 assert(can_set_nom_bit_timing(0,2,1,20,2)==FBK_ParamOutOfRange);
 assert(can_set_nom_bit_timing(0,2,139,20,21)==FBK_ParamOutOfRange);
 assert(can_select_fd_mode(0,true)==FBK_BaudrateNotSet);
 assert(can_set_data_bit_timing(0,2,29,10,10)==FBK_Success);
 assert(can_set_data_bit_timing(0,2,4,5,4)==FBK_ParamOutOfRange); // 8 Mbit/s exceeds WeAct transceiver rating.
 assert(can_inst[0].bitrate_data.Seg1==29); // Rejection leaves the configured timing intact.
 assert(can_set_data_bit_timing(0,2,11,4,4)==FBK_Success); // Exactly 5 Mbit/s.
 assert(can_set_data_bit_timing(0,2,29,10,10)==FBK_Success);
 assert(can_set_data_bit_timing(0,2,1,10,2)==FBK_ParamOutOfRange);
 assert(can_set_data_bit_timing(0,2,29,10,11)==FBK_ParamOutOfRange);
 assert(can_select_fd_mode(0,true)==FBK_Success && can_using_FD(0));
 assert(can_select_fd_mode(0,false)==FBK_Success && !can_using_FD(0));
 assert(can_inst[0].bitrate_data.Brp==2);
 assert(can_set_data_bit_timing(0,8,29,10,10)==FBK_Success);
 assert(can_select_fd_mode(0,true)==FBK_Success && can_using_FD(0));
 // Classic lower bound: reject slower timings without changing the old one.
 assert(can_set_nom_bit_timing(0,100,239,80,80)==FBK_Success); // exactly 5 kbit/s
 assert(can_set_nom_bit_timing(0,101,239,80,80)==FBK_ParamOutOfRange);
 assert(can_inst[0].bitrate_nominal.Brp==100);
 assert(can_set_nom_bit_timing(0,99,239,80,80)==FBK_Success);
 // FD lower bound is checked independently when FD is selected.
 assert(can_set_nom_bit_timing(0,26,239,80,80)==FBK_Success);
 assert(can_select_fd_mode(0,true)==FBK_ParamOutOfRange);
 assert(can_set_nom_bit_timing(0,63,100,26,20)==FBK_Success); // 160MHz/8001: just below 20k
 assert(can_select_fd_mode(0,true)==FBK_ParamOutOfRange);
 assert(can_set_nom_bit_timing(0,25,239,80,80)==FBK_Success);
 assert(can_select_fd_mode(0,true)==FBK_Success);
 assert(can_set_nom_bit_timing(0,24,239,80,80)==FBK_Success);
 assert(can_select_fd_mode(0,true)==FBK_Success);
 can_inst[0].is_open=true;
 assert(can_select_fd_mode(0,false)==FBK_AdapterMustBeClosed);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
