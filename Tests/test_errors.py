#!/usr/bin/env python3
"""Validate actual error snapshots, enqueue commit and legacy error encoding."""
import unittest
from test_regressions import ROOT, function, run_c
from test_tx import structure


class ErrorReports(unittest.TestCase):
    def test_full_pool_preserves_busoff_and_recovery(self):
        source = ROOT / 'Source/error.c'
        header = (ROOT / 'Source/error.h').read_text()
        states = header[header.index('typedef struct'):header.index('void error_init(')]
        settings = (ROOT / 'Source/settings.h').read_text(encoding='cp1252')
        def enum(name):
            end = settings.index('} ' + name + ';') + len('} ' + name + ';')
            return settings[settings.rfind('typedef enum', 0, end):end]
        definitions = '\n'.join(function(source, signature) for signature in [
            'void error_init(int channel)\n{',
            'void error_assert(int channel, eErrorAppFlags flag, bool immediately)\n{',
            'void error_update(int channel, const FDCAN_ProtocolStatusTypeDef *status,',
            'void error_snapshot(int channel, kCanErrorState *state)\n{',
            'bool error_busoff_pending(int channel)',
            'bool error_is_report_due(int channel, uint32_t now)\n{',
            'void error_report_commit(int channel, const kCanErrorState *state, uint32_t now)\n{',
        ])
        encoder = function(ROOT / 'Source/Candlelight/buffer.c', 'void buf_store_error(int channel)\n{')
        hal = ROOT / 'STM32/STM32G4xx_HAL_Driver/Inc/stm32g4xx_hal_fdcan.h'
        constants = '\n'.join(line for line in hal.read_text().splitlines() if line.startswith('#define FDCAN_PROTOCOL_ERROR_'))
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define USR_ErrorReport 1U
#define USR_Timestamp 2U
static uint32_t GLB_UserFlags[1]={USR_ErrorReport|USR_Timestamp};
static uint32_t mask_state,tick;
static uint32_t system_irq_save(void){uint32_t old=mask_state;mask_state=1;return old;}
static void system_irq_restore(uint32_t old){mask_state=old;}
static uint32_t HAL_GetTick(void){return tick;}
static uint32_t system_get_timestamp(void){return tick*1000;}
''' + enum('eErrorBusStatus') + '\n' + enum('eErrorAppFlags') + '\n' + states + '\n' +
              structure(hal, 'FDCAN_ProtocolStatusTypeDef') + '\n' +
              structure(hal, 'FDCAN_ErrorCountersTypeDef') + '\n' + constants + r'''
static err_class err_inst[1];
''' + definitions + '\n#include "' + str(ROOT / 'Source/Candlelight/candlelight_def.h') + r'''"
typedef struct {unsigned value;} list_item;
typedef struct {list_item list;kHostFrameLegacy frame;} kHostFrameObject;
typedef struct {list_item list_host_pool,list_to_host;} buf_class;
static buf_class fixture;
static kHostFrameObject object;
static bool available,GLB_ProtoElmue;
static unsigned enqueued;
static buf_class *buf_get_inst_for_usb(int channel){(void)channel;return &fixture;}
static kHostFrameObject *buf_get_frame_locked(list_item *head){(void)head;if(!available)return NULL;available=false;return &object;}
static void list_add_tail_locked(list_item *item,list_item *head){(void)item;(void)head;assert(mask_state);enqueued++;}
''' + encoder + r'''
int main(void){
 error_init(0);FDCAN_ProtocolStatusTypeDef status={0};FDCAN_ErrorCountersTypeDef counts={0};
 status.BusOff=1;status.LastErrorCode=3;counts.TxErrorCnt=255;error_update(0,&status,&counts);
 assert(error_busoff_pending(0) && error_is_report_due(0,10));
 tick=10;available=false;buf_store_error(0);
 assert(!enqueued && error_busoff_pending(0) && err_inst[0].last_tick==0);
 // Even if sampled hardware has recovered, report the retained bus-off transition first.
 status.BusOff=0;status.LastErrorCode=7;counts.TxErrorCnt=0;error_update(0,&status,&counts);
 available=true;buf_store_error(0);
 assert(enqueued==1 && !error_busoff_pending(0));
 assert((object.frame.can_id & ERID_Bus_is_off) && object.frame.pack_classic.data[6]==255);
 assert(error_is_report_due(0,10));available=true;buf_store_error(0);
 assert(enqueued==2 && (object.frame.can_id & ERID_Controller_restarted));
 assert(object.frame.pack_classic.data[1] & ER1_Bus_is_back_active);
 // CRC has a protocol location and valid counters, not the private CRC class alone.
 error_init(0);status=(FDCAN_ProtocolStatusTypeDef){.LastErrorCode=6};
 counts=(FDCAN_ErrorCountersTypeDef){.TxErrorCnt=10,.RxErrorCnt=4};error_update(0,&status,&counts);
 available=true;buf_store_error(0);
 assert((object.frame.can_id & ERID_Protocol_violation) && (object.frame.can_id & 0x200));
 assert(object.frame.pack_classic.data[3]==8 && object.frame.pack_classic.data[6]==10);
 error_init(0);status=(FDCAN_ProtocolStatusTypeDef){.ErrorPassive=1,.LastErrorCode=7};
 counts=(FDCAN_ErrorCountersTypeDef){.TxErrorCnt=128,.RxErrorCnt=3};error_update(0,&status,&counts);
 available=true;buf_store_error(0);
 assert(object.frame.pack_classic.data[1] & ER1_Tx_Passive_status_reached);
 assert(!(object.frame.pack_classic.data[1] & ER1_Rx_Passive_status_reached));
 error_assert(0,APP_CanRxFail,true);available=true;buf_store_error(0);
 assert((object.frame.can_id & ERID_Controller_problem) &&
        (object.frame.pack_classic.data[1] & ER1_Rx_Buffer_Overflow));
 assert(!(err_inst[0].cur_state.app_flags & APP_CanRxFail));
 // First app failure is immediate, even shortly after boot. Repeated
 // failures retain their flags and coalesce until 100 ms after commit.
 error_init(0);tick=10;error_assert(0,APP_CanTxFail,true);
 assert(error_is_report_due(0,tick));available=true;buf_store_error(0);
 assert(err_inst[0].reported_once && err_inst[0].last_tick==10);
 tick=20;error_assert(0,APP_CanTxFail,true);
 assert(!error_is_report_due(0,tick) && !error_is_report_due(0,109));
 assert(error_is_report_due(0,110) && err_inst[0].report_now);
 // Pool exhaustion cannot consume the flag or move the rate-limit origin.
 available=false;tick=110;buf_store_error(0);assert(err_inst[0].last_tick==10);
 assert(err_inst[0].cur_state.app_flags & APP_CanTxFail);
 available=true;buf_store_error(0);assert(err_inst[0].last_tick==110);
 // Bus-off and restart bypass the app-error limiter and retain ordering.
 tick=111;status=(FDCAN_ProtocolStatusTypeDef){.BusOff=1,.LastErrorCode=7};
 counts=(FDCAN_ErrorCountersTypeDef){.TxErrorCnt=255};error_update(0,&status,&counts);
 assert(error_is_report_due(0,tick));available=true;buf_store_error(0);
 tick=112;status.BusOff=0;counts.TxErrorCnt=0;error_update(0,&status,&counts);
 assert(error_is_report_due(0,tick));available=true;buf_store_error(0);
 assert(object.frame.can_id & ERID_Controller_restarted);
 // Unsigned rate-limit elapsed time also crosses HAL tick wrap safely.
 error_init(0);err_inst[0].reported_once=true;err_inst[0].last_tick=0xFFFFFFF0U;
 error_assert(0,APP_CanTxFail,true);
 assert(!error_is_report_due(0,0x40U) && error_is_report_due(0,0x54U));
 assert(!mask_state);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
