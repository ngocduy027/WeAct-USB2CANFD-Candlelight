#!/usr/bin/env python3
"""Execute firmware functions unchanged with modeled peripheral inputs."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HAL = ROOT / 'STM32/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd.c'


def function(path, signature):
    source = path.read_text(encoding='cp1252')
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def run_c(source):
    with tempfile.TemporaryDirectory(prefix='usb2can-native-') as tmp:
        path = Path(tmp) / 'test.c'
        path.write_text(source)
        subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Werror',
                        '-Wno-unused-parameter', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        str(path), '-o', str(path.with_suffix(''))], check=True)
        subprocess.run([str(path.with_suffix(''))], check=True)


class TransportRegression(unittest.TestCase):
    def test_bulk_packets_cannot_overwrite_destination(self):
        helper = function(HAL, 'static void PCD_ReadRxBounded(')
        receive = function(HAL, 'static uint16_t HAL_PCD_EP_DB_Receive(PCD_HandleTypeDef *hpcd,\n                                      PCD_EPTypeDef *ep, uint16_t wEPVal)\n{')
        run_c(r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef struct {void *Instance;} PCD_HandleTypeDef;
typedef struct {uint32_t num,xfer_len,xfer_capacity;uint8_t xfer_overflow;uint16_t pmaaddr0,pmaaddr1;uint8_t *xfer_buff;} PCD_EPTypeDef;
static uint16_t packet_size;
#define USB_EP_DTOG_RX 1
#define USB_EP_DTOG_TX 2
#define USB_EP_RX_NAK 0
#define PCD_GET_EP_DBUF0_CNT(a,b) packet_size
#define PCD_GET_EP_DBUF1_CNT(a,b) packet_size
#define PCD_SET_EP_RX_STATUS(a,b,c) ((void)0)
#define PCD_FREE_USER_BUFFER(a,b,c) ((void)0)
static void USB_ReadPMA(void *a,uint8_t *dst,uint16_t p,uint16_t n){memset(dst,0xA5,n);}
''' + helper + '\n' + receive + r'''
int main(void) {
  for (unsigned total=0;total<=128;total++) {
    uint8_t storage[128];memset(storage,0xCC,sizeof(storage));
    PCD_HandleTypeDef h={0};
    PCD_EPTypeDef e={.xfer_len=80,.xfer_capacity=80,.xfer_buff=storage};
    packet_size=total>64?64:total;
    unsigned count=HAL_PCD_EP_DB_Receive(&h,&e,USB_EP_DTOG_RX);
    if(total>64){packet_size=total-64;count+=HAL_PCD_EP_DB_Receive(&h,&e,0);}
    assert(count==total);
    assert(e.xfer_overflow==(total>80));
    assert(e.xfer_buff==storage+(total>80?80:total));
    for(unsigned i=80;i<128;i++)assert(storage[i]==0xCC);
  }
  return 0;
}
''')

    def test_ep0_rejects_incomplete_and_oversized_packets(self):
        stage = function(ROOT / 'Source/usb_core.c', 'USBD_StatusTypeDef USBD_LL_DataOutStage(')
        run_c(r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define USBD_EP0_DATA_OUT 1
#define USBD_EP0_STATUS_OUT 2
#define USBD_EP0_IDLE 0
#define USBD_STATE_CONFIGURED 1
#define USBD_STATE_ADDRESSED 2
#define USBD_FAIL 1
#define USBD_OK 0
#define USBD_BUSY 2
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef int USBD_StatusTypeDef;
typedef struct {uint32_t rem_length,maxpacket;} USBD_EndpointTypeDef;
struct dev;
typedef struct {int (*EP0_RxReady)(struct dev*);int (*DataOut)(struct dev*,uint8_t);} callbacks;
typedef struct dev {USBD_EndpointTypeDef ep_out[16];int ep0_state,dev_state,request;callbacks *pClass;void *pData;} USBD_HandleTypeDef;
typedef struct {struct {uint8_t xfer_overflow;} OUT_ep[8];} PCD_HandleTypeDef;
static unsigned actual,executed,status_sent,stalled,continued;
static uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef*d,unsigned ep){return actual;}
static int callback(USBD_HandleTypeDef*d){executed++;return 0;}
static void USBD_CtlContinueRx(USBD_HandleTypeDef*d,uint8_t*p,uint16_t n){continued++;}
static void USBD_CtlSendStatus(USBD_HandleTypeDef*d){status_sent++;}
static void USBD_CtlError(USBD_HandleTypeDef*d,void*r){stalled++;}
static void USBD_LL_StallEP(USBD_HandleTypeDef*d,unsigned ep){}
''' + stage + r'''
int main(void){
  callbacks c={.EP0_RxReady=callback};
  for(unsigned length=0;length<=64;length++) {
    PCD_HandleTypeDef p={0};
    USBD_HandleTypeDef d={.ep0_state=USBD_EP0_DATA_OUT,.dev_state=USBD_STATE_CONFIGURED,.pClass=&c,.pData=&p};
    d.ep_out[0]=(USBD_EndpointTypeDef){.rem_length=20,.maxpacket=64};
    executed=status_sent=stalled=continued=0;actual=length;
    USBD_LL_DataOutStage(&d,0,NULL);
    assert(executed==(length==20));assert(status_sent==(length==20));
    assert(stalled==(length!=20));assert(continued==0);
  }
  PCD_HandleTypeDef p={0};
  USBD_HandleTypeDef d={.ep0_state=USBD_EP0_DATA_OUT,.dev_state=USBD_STATE_CONFIGURED,.pClass=&c,.pData=&p};
  d.ep_out[0]=(USBD_EndpointTypeDef){.rem_length=80,.maxpacket=64};
  executed=status_sent=stalled=continued=0;actual=64;
  USBD_LL_DataOutStage(&d,0,NULL);
  assert(continued==1 && d.ep_out[0].rem_length==16 && !executed);
  actual=16;USBD_LL_DataOutStage(&d,0,NULL);
  assert(executed==1 && status_sent==1 && !stalled);
  d.dev_state=USBD_STATE_ADDRESSED;d.ep0_state=USBD_EP0_DATA_OUT;
  d.ep_out[0]=(USBD_EndpointTypeDef){.rem_length=20,.maxpacket=64};actual=20;
  executed=status_sent=stalled=continued=0;USBD_LL_DataOutStage(&d,0,NULL);
  assert(!executed && !status_sent && stalled==1 && !continued);
  return 0;
}
''')

    def test_string_descriptor_bounds(self):
        get_string = function(ROOT / 'Source/usb_ctrlreq.c', 'uint8_t* USBD_GetStringDescr(')
        run_c(r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define USB_DESC_TYPE_STRING 3
static uint8_t USBD_StrDesc[512];
''' + get_string + r'''
int main(void){
 char input[300];memset(input,'x',sizeof(input));
 for(unsigned length=0;length<sizeof(input);length++){
   input[length]=0;memset(USBD_StrDesc,0xCC,sizeof(USBD_StrDesc));uint16_t n=0;
   assert(USBD_GetStringDescr(input,&n)==USBD_StrDesc);
   unsigned chars=length>126?126:length;
   assert(n==chars*2+2 && USBD_StrDesc[0]==n && USBD_StrDesc[1]==3);
   for(unsigned j=n;j<512;j++)assert(USBD_StrDesc[j]==0xCC);
   input[length]='x';
 }
 assert(USBD_GetStringDescr(NULL,NULL)==USBD_StrDesc);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
