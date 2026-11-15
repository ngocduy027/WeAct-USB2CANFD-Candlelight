#!/usr/bin/env python3
"""Exercise actual queue staging, IN ownership and reset processing with USB MMIO modeled."""
import subprocess
import tempfile
from pathlib import Path
import unittest
from test_regressions import ROOT, function


class UsbLifecycle(unittest.TestCase):
    def test_backpressure_clear_halt_and_reset(self):
        header = (ROOT / 'Source/Candlelight/buffer.h').read_text(encoding='cp1252')
        types = header[header.index('#define container_of'):header.index('void buf_init();')]
        buffer = ROOT / 'Source/Candlelight/buffer.c'
        usb = ROOT / 'Source/Candlelight/usb_class.c'
        funcs = '\n'.join(function(buffer, sig) for sig in [
            'void buf_init()\n{',
            'void buf_clear_buffers(int channel, bool clear_can, bool clear_host)\n{',
            'kHostFrameObject* buf_get_frame_locked(list_item* list_head)\n{',
            'void buf_process_host(int channel, buf_class* usb_buf)\n{',
            'void buf_release_in_flight(int channel)\n{',
        ]) + '\n' + '\n'.join(function(usb, sig) for sig in [
            'bool USBD_TrafficReady(void)\n{',
            'void USBD_RequestSessionReset(void)\n{',
            'void USBD_CancelIn(int channel)\n{',
            'void USBD_EndpointClearHalt(uint8_t address)\n{',
            'void USBD_InterfaceReset(uint8_t interface)\n{',
            'void USBD_ProcessOut(void)\n{',
            'void USBD_ProcessLifecycle(void)\n{',
            'uint8_t USBD_GS_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)\n{',
            'uint8_t USBD_GS_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)\n{',
        ])
        source = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define __IO volatile
#define CHANNEL_COUNT 2
#define CANDLE_INRERFACE_COUNT 1
#define DFU_INTERFACE_NUMBER 1
#define CAN_QUEUE_SIZE 64
#define HOST_QUEUE_SIZE 70
#define USBD_STATE_CONFIGURED 3
#define USBD_OK 0
#define USBD_FAIL 1
#define USR_CandleDefault 0x300
#define APP_CanTxFail 1
#define MIN(a,b) ((a)<(b)?(a):(b))
static uint32_t mask_state;
static uint32_t system_irq_save(void){uint32_t old=mask_state;mask_state=1;return old;}
static void system_irq_restore(uint32_t old){mask_state=old;}
#include "candlelight_def.h"
#include "frame_codec.h"
''' + types + r'''
static buf_class buf_inst[CHANNEL_COUNT];
static bool GLB_ProtoElmue,usb_ready,reset_pending;
static uint32_t GLB_UserFlags[CHANNEL_COUNT];
typedef struct {uint8_t *xfer_buff;uint32_t xfer_len,xfer_count,xfer_overflow;} ep;
typedef struct {void *Instance;ep IN_ep[8],OUT_ep[8];} PCD_HandleTypeDef;
typedef struct {struct {unsigned is_used;} ep_in[8];unsigned dev_state;PCD_HandleTypeDef *pData;} USBD_HandleTypeDef;
static PCD_HandleTypeDef pcd;
static USBD_HandleTypeDef GLB_UsbDevice={.pData=&pcd,.dev_state=USBD_STATE_CONFIGURED};
static uint8_t EndpointsIN[]={0x81},EndpointsOUT[]={2},EpToChannel[16];
static unsigned aborts,out_aborts,toggle_clears,ctr_clears,arms,rx_size,send_count,errors,closes,suspends,resumes;
static unsigned sent_id;
static bool suspended,inject_reset_during_close;
static buf_class *buf_get_instance(int channel){assert(channel>=0 && channel<CHANNEL_COUNT);return &buf_inst[channel];}
static void HAL_PCD_EP_Abort(PCD_HandleTypeDef *p,uint8_t address){assert(mask_state && (address==0x81 || address==2));if(address==0x81)aborts++;else out_aborts++;}
static void USBD_LL_ClearStallEP(USBD_HandleTypeDef *p,uint8_t address){assert(address==0x81 || address==2);toggle_clears++;}
#define PCD_CLEAR_TX_EP_CTR(instance,ep) (ctr_clears++)
static unsigned USBD_LL_GetRxDataSize(USBD_HandleTypeDef *d,uint8_t epnum){return rx_size;}
static void USBD_LL_PrepareReceive(USBD_HandleTypeDef *d,uint8_t address,uint8_t *p,unsigned n){assert(mask_state && n==80 && address==2);arms++;}
static void error_assert(int channel,int flag,bool immediate){errors++;}
static bool HAL_PCD_Is_Suspended(void){return suspended;}
static void can_suspend_all(void){suspends++;}
static void can_resume_all(void){resumes++;}
static void USBD_RequestSessionReset(void);
static void can_close_all(void){assert(!mask_state);closes++;if(inject_reset_during_close)USBD_RequestSessionReset();}
static int USBD_LL_Transmit(USBD_HandleTypeDef *d,uint8_t address,void *data,unsigned n){assert(!n && !data);send_count++;return USBD_OK;}
static void USBD_SendFrameToHost(int channel,void *frame){sent_id=((kHostFrameLegacy*)frame)->echo_id;send_count++;buf_inst[channel].TxBusy=true;}
static bool USBD_TrafficReady(void);
static void buf_clear_buffers(int channel,bool clear_can,bool clear_host);
kHostFrameObject *buf_get_frame_locked(list_item *head);
''' + funcs + r'''
static kHostFrameObject *queue_host(unsigned id){
 kHostFrameObject *p=buf_get_frame_locked(&buf_inst[0].list_host_pool);assert(p);
 p->frame.echo_id=id;list_add_tail(&p->list,&buf_inst[0].list_to_host);return p;
}
int main(void){
 buf_init();usb_ready=true;GLB_UsbDevice.ep_in[1].is_used=1;
 // A valid record waits staged when the CAN pool is full. Hardware is not rearmed.
 kHostFrameObject *held[64];for(unsigned i=0;i<64;i++)held[i]=buf_get_frame_locked(&buf_inst[1].list_can_pool);
 kHostFrameLegacy f={.echo_id=7,.can_id=0x123,.can_dlc=8,.channel=1};
 memcpy(buf_inst[0].from_host_buf,&f,20);rx_size=20;
 assert(USBD_GS_DataOut(&GLB_UsbDevice,2)==USBD_OK && buf_inst[0].RxPending);
 USBD_ProcessOut();assert(!arms && buf_inst[0].RxPending && list_is_empty(&buf_inst[1].list_to_can));
 list_add_tail(&held[0]->list,&buf_inst[1].list_can_pool);USBD_ProcessOut();
 assert(arms==1 && !buf_inst[0].RxPending && count_free_entries(&buf_inst[1].list_to_can)==1);
 kHostFrameObject *received=buf_get_frame_locked(&buf_inst[1].list_to_can);
 assert(received->frame.echo_id==7 && !received->rejected);
 // Oversized/malformed record never reaches CAN, but its valid legacy context is returned as rejection.
 memcpy(buf_inst[0].from_host_buf,&f,20);pcd.OUT_ep[2].xfer_overflow=1;rx_size=128;
 USBD_GS_DataOut(&GLB_UsbDevice,2);USBD_ProcessOut();assert(buf_inst[0].RxPending);
 list_add_tail(&received->list,&buf_inst[1].list_can_pool);USBD_ProcessOut();
 received=buf_get_frame_locked(&buf_inst[1].list_to_can);assert(received && received->rejected && received->frame.echo_id==7);
 // IN object is retained until final USB completion and replayed after clear-halt.
 kHostFrameObject *first=queue_host(9);queue_host(10);unsigned before=send_count;
 buf_process_host(0,&buf_inst[0]);assert(buf_inst[0].in_flight==first && send_count==before+1 && sent_id==9);
 buf_process_host(0,&buf_inst[0]);assert(send_count==before+1);
 buf_inst[0].SendZLP=true;pcd.IN_ep[1].xfer_buff=buf_inst[0].to_host_buf;pcd.IN_ep[1].xfer_len=64;
 USBD_InterfaceReset(0);assert(toggle_clears==2 && aborts==1 && ctr_clears==1 && !buf_inst[0].TxBusy && !buf_inst[0].SendZLP);
 assert(!pcd.IN_ep[1].xfer_buff && !pcd.IN_ep[1].xfer_len && buf_inst[0].in_flight==first);
 buf_process_host(0,&buf_inst[0]);assert(sent_id==9 && buf_inst[0].in_flight==first);
 buf_inst[0].SendZLP=true;USBD_GS_DataIn(&GLB_UsbDevice,1);assert(buf_inst[0].in_flight==first && !buf_inst[0].SendZLP);
 USBD_GS_DataIn(&GLB_UsbDevice,1);assert(!buf_inst[0].in_flight && !buf_inst[0].TxBusy);
 buf_process_host(0,&buf_inst[0]);assert(sent_id==10);
 // Clear-halt OUT cannot discard a record already staged under backpressure.
 buf_inst[0].RxPending=true;before=arms;USBD_EndpointClearHalt(2);assert(arms==before && out_aborts==1);
 before=toggle_clears;USBD_InterfaceReset(1);assert(toggle_clears==before); // DFU has no bulk endpoints.
 usb_ready=false;buf_inst[0].RxPending=false;before=arms;USBD_EndpointClearHalt(2);
 assert(arms==before && out_aborts==2);usb_ready=true;
 // Reset ISR only requests teardown. Main owns all queue reconstruction.
 GLB_ProtoElmue=true;GLB_UserFlags[1]=0xFFFF;USBD_RequestSessionReset();
 assert(!USBD_TrafficReady() && buf_inst[0].in_flight && !closes);
 inject_reset_during_close=true;USBD_ProcessLifecycle();assert(closes==1 && reset_pending && !USBD_TrafficReady());
 inject_reset_during_close=false;USBD_ProcessLifecycle();assert(closes==2 && USBD_TrafficReady() && !GLB_ProtoElmue);
 for(unsigned c=0;c<2;c++){
  assert(GLB_UserFlags[c]==USR_CandleDefault && count_free_entries(&buf_inst[c].list_can_pool)==64);
  assert(count_free_entries(&buf_inst[c].list_host_pool)==70 && !buf_inst[c].in_flight);
 }
 GLB_UsbDevice.ep_in[1].is_used=0;before=aborts;USBD_CancelIn(0);assert(aborts==before);
 suspended=true;USBD_ProcessLifecycle();assert(suspends==1 && resumes>=2 && !mask_state);
 return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='usb2can-lifecycle-') as tmp:
            path = Path(tmp) / 'test.c'
            path.write_text(source)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Werror', '-Wno-unused-parameter',
                            '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I' + str(ROOT / 'Source/Candlelight'), str(path),
                            str(ROOT / 'Source/Candlelight/frame_codec.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
