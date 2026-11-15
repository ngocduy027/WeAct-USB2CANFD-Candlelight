#!/usr/bin/env python3
"""Run real Chapter 9 request handlers against modeled endpoint state."""
import re
import unittest
from test_regressions import ROOT, function, run_c
from test_tx import structure


class UsbRequests(unittest.TestCase):
    def test_interface_reset_maps_all_can_interfaces_and_skips_dfu(self):
        reset = function(ROOT / 'Source/Candlelight/usb_class.c', 'void USBD_InterfaceReset(')
        run_c(r'''
#include <stdint.h>
#include <assert.h>
#define DFU_INTERFACE_NUMBER 1
#define CANDLE_INRERFACE_COUNT 3
static unsigned GLB_UsbDevice;
static uint8_t EndpointsIN[]={0x81,0x83,0x85},EndpointsOUT[]={2,4,6};
static unsigned calls[4],count;
static void USBD_LL_ClearStallEP(void *d,uint8_t ep){assert(d==&GLB_UsbDevice);calls[count++]=ep;}
static void USBD_EndpointClearHalt(uint8_t ep){calls[count++]=0x100U|ep;}
''' + reset + r'''
int main(void){
 const uint8_t interfaces[]={0,2,3},in[]={0x81,0x83,0x85},out[]={2,4,6};
 for(unsigned i=0;i<256;i++){
  count=0;USBD_InterfaceReset(i);unsigned valid=0;
  for(unsigned c=0;c<3;c++)if(i==interfaces[c]){
   valid=1;assert(count==4 && calls[0]==in[c] && calls[1]==(0x100U|in[c]));
   assert(calls[2]==out[c] && calls[3]==(0x100U|out[c]));
  }
  if(!valid)assert(!count);
 }
 return 0;
}
''')

    def test_microsoft_request_uses_fresh_ep0_state(self):
        header = ROOT / 'Source/usb_def.h'
        constants = '\n'.join(line for line in header.read_text().splitlines()
                              if re.match(r'#define\s+(USB_REQ_|USBD_EP0_)', line))
        source = ROOT / 'Source/Candlelight/usb_class.c'
        funcs = '\n'.join(function(source, sig) for sig in [
            'bool USBD_GS_CustomRequest(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)\n{',
            'bool USBD_SetupStageRequest(PCD_HandleTypeDef *hpcd)\n{',
        ])
        parser = function(ROOT / 'Source/usb_ctrlreq.c', 'void USBD_ParseSetupRequest(')
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#define USBD_MS_OS_VENDOR_CODE 0x20
#define USBD_INTERFACES_COUNT 2
#define DFU_INTERFACE_NUMBER 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define SWAPBYTE(p) ((uint16_t)((p)[0] | ((uint16_t)(p)[1]<<8)))
''' + constants + '\n' + structure(header, 'USBD_SetupReqTypedef') + r'''
typedef struct {USBD_SetupReqTypedef request;unsigned ep0_state,ep0_data_len;} USBD_HandleTypeDef;
typedef struct {void *pData;uint8_t Setup[8];} PCD_HandleTypeDef;
static uint8_t USBD_MicrosoftFeatureDescr[64],USBD_MicrosoftExtPropertyDescr[142],strings[512];
static unsigned length,sends;
static uint8_t *response;
static uint8_t *USBD_GetStringDescr(char *s,uint16_t *len){return strings;}
static void USBD_CtlSendData(USBD_HandleTypeDef *d,uint8_t *p,unsigned n){
 assert(d->ep0_state==USBD_EP0_SETUP && d->ep0_data_len==d->request.wLength);
 response=p;length=n;sends++;
}
''' + parser + '\n' + funcs + r'''
int main(void){
 USBD_HandleTypeDef d={0};PCD_HandleTypeDef p={.pData=&d};
 for(unsigned type=0;type<256;type++){
  USBD_SetupReqTypedef r={.bRequestType=type,.bRequest=0x20,.wIndex=4,.wLength=64};
  memcpy(p.Setup,&r,8);d.ep0_state=USBD_EP0_DATA_OUT;d.ep0_data_len=512;
  bool handled=USBD_SetupStageRequest(&p);
  bool valid=type==0xC0 || type==0xC1;
  assert(handled==valid && d.ep0_state==USBD_EP0_SETUP && d.ep0_data_len==64);
  if(valid)assert(length==64 && response==USBD_MicrosoftFeatureDescr);
 }
 USBD_SetupReqTypedef r={.bRequestType=0xC0,.bRequest=0x20,.wIndex=5,.wValue=1,.wLength=7};
 memcpy(p.Setup,&r,8);assert(USBD_SetupStageRequest(&p) && length==7 && response==strings && strings[70]=='2');
 r.wLength=512;memcpy(p.Setup,&r,8);assert(USBD_SetupStageRequest(&p) && length==142);
 r.wLength=0;memcpy(p.Setup,&r,8);assert(!USBD_SetupStageRequest(&p));
 r.wLength=64;r.wValue=2;memcpy(p.Setup,&r,8);assert(!USBD_SetupStageRequest(&p));
 r.wValue=0;r.wIndex=6;memcpy(p.Setup,&r,8);assert(!USBD_SetupStageRequest(&p));
 assert(sends==4);return 0;
}
''')

    def test_endpoint_and_interface_validation(self):
        header = ROOT / 'Source/usb_def.h'
        constants = '\n'.join(line for line in header.read_text().splitlines()
                              if re.match(r'#define\s+(USB_REQ_|USB_FEATURE_|USBD_STATE_|USBD_EP0_)', line))
        source = ROOT / 'Source/usb_ctrlreq.c'
        funcs = '\n'.join(function(source, sig) for sig in [
            'USBD_StatusTypeDef USBD_StdEPReq(', 'USBD_StatusTypeDef USBD_StdItfReq('])
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#define USBD_OK 0
#define USBD_FAIL 1
#define USBD_BUSY 2
#define Candlelight 1
#define USBD_INTERFACES_COUNT 2
#define LOBYTE(x) ((uint8_t)(x))
typedef int USBD_StatusTypeDef;
''' + constants + '\n' + structure(header, 'USBD_SetupReqTypedef') + '\n' +
              structure(header, 'USBD_EndpointTypeDef') + r'''
struct dev;
typedef struct {int (*Setup)(struct dev *,USBD_SetupReqTypedef *);} callbacks;
typedef struct dev {USBD_EndpointTypeDef ep_in[16],ep_out[16];unsigned dev_state,ep0_state;callbacks *pClass;} USBD_HandleTypeDef;
static unsigned errors,sends,statuses,clears,stalls,setup_calls;
static unsigned response_length,response_value,interface_resets;
static int setup_result;
static bool endpoint_halted;
static void USBD_CtlError(USBD_HandleTypeDef *d,USBD_SetupReqTypedef *r){errors++;d->ep0_state=USBD_EP0_STALL;}
static void USBD_CtlSendData(USBD_HandleTypeDef *d,uint8_t *p,unsigned n){sends++;response_length=n;response_value=p[0];d->ep0_state=USBD_EP0_DATA_IN;}
static void USBD_CtlSendStatus(USBD_HandleTypeDef *d){statuses++;d->ep0_state=USBD_EP0_STATUS_IN;}
static bool USBD_LL_IsStallEP(USBD_HandleTypeDef *d,unsigned ep){assert(ep==0x81 || ep==2);return endpoint_halted;}
static void USBD_LL_StallEP(USBD_HandleTypeDef *d,unsigned ep){assert(ep==0x81 || ep==2);stalls++;endpoint_halted=true;}
static void USBD_LL_ClearStallEP(USBD_HandleTypeDef *d,unsigned ep){assert(ep==0x81 || ep==2);clears++;endpoint_halted=false;}
static void USBD_EndpointClearHalt(unsigned ep){assert(ep==0x81 || ep==2);clears++;}
static void USBD_InterfaceReset(uint8_t interface){assert(interface<2);interface_resets++;}
static int setup(USBD_HandleTypeDef *d,USBD_SetupReqTypedef *r){setup_calls++;if(setup_result==USBD_FAIL)USBD_CtlError(d,r);return setup_result;}
''' + funcs + r'''
static void reset(USBD_HandleTypeDef *d){errors=sends=statuses=clears=stalls=interface_resets=0;d->ep0_state=USBD_EP0_SETUP;}
int main(void){
 callbacks cb={setup};USBD_HandleTypeDef d={.dev_state=USBD_STATE_CONFIGURED,.pClass=&cb};
 d.ep_in[0].is_used=d.ep_out[0].is_used=d.ep_in[1].is_used=d.ep_out[2].is_used=1;
 for(unsigned address=0;address<256;address++){
  bool valid=address==0 || address==0x80 || address==0x81 || address==2;
  USBD_SetupReqTypedef r={.bRequestType=0x82,.bRequest=USB_REQ_GET_STATUS,.wIndex=address,.wLength=2};
  reset(&d);int ret=USBD_StdEPReq(&d,&r);
  assert((ret==USBD_OK)==valid);assert(sends==valid && errors==!valid);
  r.wIndex|=0x100;reset(&d);assert(USBD_StdEPReq(&d,&r)==USBD_FAIL && errors==1);
  r.wIndex=address;r.bRequestType=2;r.wLength=0;r.bRequest=USB_REQ_SET_FEATURE;
  reset(&d);valid=address==0x81 || address==2;
  ret=USBD_StdEPReq(&d,&r);assert((ret==USBD_OK)==valid);assert(stalls==valid && statuses==valid && errors==!valid);
  r.bRequest=USB_REQ_CLEAR_FEATURE;reset(&d);ret=USBD_StdEPReq(&d,&r);
  assert((ret==USBD_OK)==valid);assert(clears==2*valid && statuses==valid && errors==!valid);
 }
 for(unsigned index=0;index<512;index++){
  USBD_SetupReqTypedef r={.bRequestType=0x81,.bRequest=USB_REQ_GET_STATUS,.wIndex=index,.wLength=2};
  reset(&d);int ret=USBD_StdItfReq(&d,&r);assert((ret==USBD_OK)==(index<2));
  if(index<2)assert(response_length==2 && response_value==0);
  r.bRequest=USB_REQ_GET_INTERFACE;r.wLength=1;reset(&d);ret=USBD_StdItfReq(&d,&r);
  assert((ret==USBD_OK)==(index<2));if(index<2)assert(response_length==1 && response_value==0);
 }
 USBD_SetupReqTypedef r={.bRequestType=0x81,.bRequest=USB_REQ_GET_STATUS,.wLength=2};
 r.wValue=1;reset(&d);assert(USBD_StdItfReq(&d,&r)==USBD_FAIL);
 r.wValue=0;r.bRequestType=1;reset(&d);assert(USBD_StdItfReq(&d,&r)==USBD_FAIL);
 r.bRequest=USB_REQ_SET_INTERFACE;r.wLength=0;
 for(unsigned interface=0;interface<2;interface++)for(unsigned alt=0;alt<256;alt++){
  r.wIndex=interface;r.wValue=alt;reset(&d);int ret=USBD_StdItfReq(&d,&r);
  assert((ret==USBD_OK)==(alt==0));assert(statuses==(alt==0) && interface_resets==(alt==0));
 }
 r.wIndex=0;r.wValue=0;r.wLength=1;reset(&d);assert(USBD_StdItfReq(&d,&r)==USBD_FAIL && !interface_resets);
 r.wLength=0;r.bRequestType=0x81;reset(&d);assert(USBD_StdItfReq(&d,&r)==USBD_FAIL && !interface_resets);
 r.bRequestType=1;r.wIndex=2;reset(&d);assert(USBD_StdItfReq(&d,&r)==USBD_FAIL && !interface_resets);
 r=(USBD_SetupReqTypedef){.bRequestType=0x41};setup_result=USBD_FAIL;reset(&d);
 assert(USBD_StdItfReq(&d,&r)==USBD_FAIL && errors==1 && !statuses && setup_calls==1);
 setup_result=USBD_BUSY;reset(&d);assert(USBD_StdItfReq(&d,&r)==USBD_BUSY && !statuses);
 setup_result=USBD_OK;reset(&d);assert(USBD_StdItfReq(&d,&r)==USBD_OK && statuses==1);
 d.dev_state=USBD_STATE_ADDRESSED;r=(USBD_SetupReqTypedef){.bRequestType=0x82,.wLength=2};
 reset(&d);assert(USBD_StdEPReq(&d,&r)==USBD_OK);r.wIndex=0x81;
 reset(&d);assert(USBD_StdEPReq(&d,&r)==USBD_FAIL);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
