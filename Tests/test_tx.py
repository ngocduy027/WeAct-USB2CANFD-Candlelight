#!/usr/bin/env python3
"""Exercise real TX ownership code with modeled CAN submission/completion."""
import unittest
from test_regressions import ROOT, function, run_c


def structure(path, name):
    text = path.read_text(encoding='cp1252')
    end = text.index('} ' + name + ';') + len('} ' + name + ';')
    return text[text.rfind('typedef struct', 0, end):end]


class TxOwnership(unittest.TestCase):
    def test_completion_cancel_backpressure_and_pool_conservation(self):
        header = (ROOT / 'Source/Candlelight/buffer.h').read_text(encoding='cp1252')
        types = header[header.index('#define container_of'):header.index('void buf_init();')]
        source = ROOT / 'Source/Candlelight/buffer.c'
        definitions = '\n'.join(function(source, signature) for signature in [
            'void buf_clear_buffers(int channel, bool clear_can, bool clear_host)\n{',
            'kHostFrameObject* buf_get_frame_locked(list_item* list_head)\n{',
            'void buf_tx_finish(int channel, uint8_t marker, uint32_t timestamp, bool success)\n{',
            'unsigned buf_tx_pending_count(int channel)\n{',
            'bool buf_tx_timeout_due(int channel, uint32_t now)\n{',
            'void buf_tx_reconcile(int channel, uint32_t pending, uint32_t occurred, uint32_t canceled)\n{',
            'void buf_tx_cancel_all(int channel)\n{',
            'void buf_process_can(int channel, buf_class* usb, buf_class* can)\n{',
        ])
        hal = ROOT / 'STM32/STM32G4xx_HAL_Driver/Inc/stm32g4xx_hal_fdcan.h'
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define __IO volatile
#define CHANNEL_COUNT 1
#define CAN_QUEUE_SIZE 64
#define HOST_QUEUE_SIZE 70
#define MIN(a,b) ((a)<(b)?(a):(b))
#define FBK_Success 2
#define FBK_BusIsOff 56
#define FRMC(name,value) enum {name=value}
FRMC(FDCAN_EXTENDED_ID,1);FRMC(FDCAN_STANDARD_ID,0);
FRMC(FDCAN_REMOTE_FRAME,1);FRMC(FDCAN_DATA_FRAME,0);
FRMC(FDCAN_FD_CAN,1);FRMC(FDCAN_CLASSIC_CAN,0);
FRMC(FDCAN_BRS_ON,1);FRMC(FDCAN_BRS_OFF,0);
FRMC(FDCAN_ESI_PASSIVE,1);FRMC(FDCAN_ESI_ACTIVE,0);
FRMC(FDCAN_STORE_TX_EVENTS,1);
enum {APP_CanTxFail=2};
enum {USR_ReportTX=1,USR_Timestamp=2};
typedef int eFeedback;
static uint32_t tick,mask_state;
static uint32_t system_irq_save(void){uint32_t old=mask_state;mask_state=1;return old;}
static void system_irq_restore(uint32_t old){mask_state=old;}
static uint32_t HAL_GetTick(void){return tick;}
static uint32_t system_get_timestamp(void){return tick*1000;}
''' + '#include "' + str(ROOT / 'Source/Candlelight/candlelight_def.h') + '"\n' +
              structure(hal, 'FDCAN_TxHeaderTypeDef') + '\n' + types + r'''
static buf_class buf_inst[1];
static bool GLB_ProtoElmue;
static uint32_t GLB_UserFlags[1]={USR_ReportTX|USR_Timestamp};
static unsigned error_count,send_count;
static uint8_t sent_markers[256];
static int allowed=FBK_Success;
static bool fifo_free=true,can_fd=true,submit_ok=true;
static void error_assert(int channel,int flags,bool immediate){(void)channel;(void)flags;(void)immediate;error_count++;}
static buf_class *buf_get_inst_for_usb(int channel){return &buf_inst[channel];}
static int can_is_tx_allowed(int channel){(void)channel;return allowed;}
static bool can_is_tx_fifo_free(int channel){(void)channel;return fifo_free;}
static bool can_using_FD(int channel){(void)channel;return can_fd;}
static bool can_is_passive(int channel){(void)channel;return false;}
static void *can_get_handle(int channel){(void)channel;return NULL;}
static uint32_t HAL_FDCAN_GetLatestTxFifoQRequestBuffer(void *handle){(void)handle;return 1;}
static uint8_t utils_byte_count_to_dlc(unsigned bytes){return bytes>8?9:bytes;}
static bool can_send_packet(int channel,FDCAN_TxHeaderTypeDef *header,uint8_t *data){
 (void)channel;(void)data;assert(mask_state);
 sent_markers[send_count++]=header->MessageMarker;return submit_ok;
}
kHostFrameObject *buf_get_frame_locked(list_item *list_head);
''' + definitions + r'''
static void enqueue(unsigned id,bool reject){
 kHostFrameObject *obj=buf_get_frame_locked(&buf_inst[0].list_can_pool);assert(obj);
 memset(&obj->frame,0,sizeof(obj->frame));
 obj->frame.echo_id=id;obj->frame.can_id=0x123;obj->frame.can_dlc=8;obj->rejected=reject;
 list_add_tail(&obj->list,&buf_inst[0].list_to_can);
}
static unsigned seen[100];
static unsigned consume(void){
 unsigned count=0;kHostFrameObject *obj;
 while((obj=buf_get_frame_locked(&buf_inst[0].list_to_host))){
  assert(obj->frame.echo_id<100);seen[obj->frame.echo_id]++;count++;
  list_add_tail(&obj->list,&buf_inst[0].list_host_pool);
 }
 return count;
}
static void balanced(void){
 assert(count_free_entries(&buf_inst[0].list_can_pool)==64);
 assert(count_free_entries(&buf_inst[0].list_host_pool)==70);
 assert(!buf_tx_pending_count(0));assert(!mask_state);
}
int main(void){
 buf_clear_buffers(0,true,true);
 for(unsigned i=0;i<10;i++)enqueue(i,false);
 for(unsigned i=0;i<4;i++)buf_process_can(0,&buf_inst[0],&buf_inst[0]);
 assert(send_count==3 && buf_tx_pending_count(0)==3);
 assert(list_is_empty(&buf_inst[0].list_to_host)); // No echo before actual completion.
 assert(count_free_entries(&buf_inst[0].list_to_can)==7);
 assert(!buf_tx_timeout_due(0,499));assert(buf_tx_timeout_due(0,500));
 uint8_t first=sent_markers[0];buf_tx_finish(0,first,12345,true);
 buf_tx_finish(0,first,12345,true); // Duplicate event must not emit a second echo.
 assert(consume()==1 && seen[0]==1);
 buf_tx_cancel_all(0);assert(consume()==2 && !buf_tx_pending_count(0));
 assert(!buf_tx_timeout_due(0,600));
 for(unsigned i=0;i<20 && !list_is_empty(&buf_inst[0].list_to_can);i++){
  tick+=600;buf_process_can(0,&buf_inst[0],&buf_inst[0]);buf_tx_cancel_all(0);consume();
 }
 for(unsigned i=0;i<10;i++)assert(seen[i]==1);
 balanced();
 // Exhaust the host pool: the request stays queued until completion capacity exists.
 kHostFrameObject *held[70];
 for(unsigned i=0;i<70;i++){held[i]=buf_get_frame_locked(&buf_inst[0].list_host_pool);assert(held[i]);}
 enqueue(20,false);unsigned sent_before=send_count;
 buf_process_can(0,&buf_inst[0],&buf_inst[0]);assert(send_count==sent_before);
 assert(count_free_entries(&buf_inst[0].list_to_can)==1);
 list_add_tail(&held[0]->list,&buf_inst[0].list_host_pool);
 buf_process_can(0,&buf_inst[0],&buf_inst[0]);assert(send_count==sent_before+1);
 buf_tx_reconcile(0,0,0,0);assert(buf_tx_pending_count(0)==1); // No completion evidence.
 buf_tx_reconcile(0,0,0,1);assert(!buf_tx_pending_count(0));assert(consume()==1);
 for(unsigned i=1;i<70;i++)list_add_tail(&held[i]->list,&buf_inst[0].list_host_pool);
 balanced();
 // A stale marker cannot finish a different request after its slot is reused.
 enqueue(21,false);buf_process_can(0,&buf_inst[0],&buf_inst[0]);
 uint8_t latest=sent_markers[send_count-1];assert(latest!=first);
 buf_tx_finish(0,first,1,true);assert(buf_tx_pending_count(0)==1);
 buf_tx_finish(0,latest,2,true);assert(consume()==1);balanced();
 enqueue(22,true);sent_before=send_count;
 buf_process_can(0,&buf_inst[0],&buf_inst[0]);assert(send_count==sent_before);
 assert(consume()==1 && seen[22]==1);balanced();
 // Bus-off retains a queued request and its host context until recovery.
 enqueue(23,false);allowed=FBK_BusIsOff;buf_process_can(0,&buf_inst[0],&buf_inst[0]);
 assert(count_free_entries(&buf_inst[0].list_to_can)==1 && !buf_tx_pending_count(0));
 allowed=FBK_Success;buf_process_can(0,&buf_inst[0],&buf_inst[0]);buf_tx_cancel_all(0);consume();balanced();
 // ElmueSoft echoes carry the user's marker, never the internal tracker marker.
 GLB_ProtoElmue=true;
 kHostFrameObject *obj=buf_get_frame_locked(&buf_inst[0].list_can_pool);assert(obj);
 memset(&obj->frame,0,sizeof(obj->frame));obj->rejected=false;
 kTxFrameElmue *elm=(void *)&obj->frame;elm->header.size=8;elm->header.msg_type=MSG_TxFrame;
 elm->can_id=0x123;elm->marker=0xA2;
 list_add_tail(&obj->list,&buf_inst[0].list_to_can);
 buf_process_can(0,&buf_inst[0],&buf_inst[0]);
 buf_tx_finish(0,sent_markers[send_count-1],45678,true);
 obj=buf_get_frame_locked(&buf_inst[0].list_to_host);assert(obj);
 kTxEchoElmue *echo=(void *)&obj->frame;
 assert(echo->header.msg_type==MSG_TxEcho && echo->marker==0xA2 && echo->timestamp==45678);
 list_add_tail(&obj->list,&buf_inst[0].list_host_pool);balanced();
 assert(error_count>=1);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
