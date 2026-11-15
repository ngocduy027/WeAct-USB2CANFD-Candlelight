#!/usr/bin/env python3
"""Test deferred EP0 ownership and nested interrupt-mask preservation."""
import unittest
from test_regressions import ROOT, function, run_c


class DeferredControl(unittest.TestCase):
    def test_unconfigured_out_setup_never_stages_or_acknowledges_commands(self):
        setup = function(ROOT / 'Source/Candlelight/control.c', 'bool control_setup_request(')
        settings = (ROOT / 'Source/settings.h').read_text(encoding='cp1252')
        end = settings.index('} eFeedback;') + len('} eFeedback;')
        feedback = settings[settings.rfind('typedef enum', 0, end):end]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define CHANNEL_COUNT 1
#define LED_CAN_ACTIVITY_ONLY
#define MAX_FLASH_DATA_LEN 2000
#define USB_REQ_DIRECTION_MASK 0x80
#define USB_REQ_DIRECTION_IN 0x80
#define USB_REQ_DIRECTION_OUT 0
#define USBD_STATE_CONFIGURED 3
#define USBD_EP0_DATA_OUT 2
#define OPT_BOOT0_Enable 2
#define MIN(a,b) ((a)<(b)?(a):(b))
''' + feedback + '\n#include "' + str(ROOT / 'Source/Candlelight/candlelight_def.h') + r'''"
typedef struct {uint8_t bRequestType,bRequest;uint16_t wValue,wIndex,wLength;} USBD_SetupReqTypedef;
typedef struct {unsigned dev_state,ep0_state;} USBD_HandleTypeDef;
static bool GLB_ProtoElmue=true,command_pending;
static uint8_t ep0_buf[2008];static USBD_SetupReqTypedef last_setup_request;
static eFeedback ELM_LastError;static unsigned prepares,sends;
static kCapabilityClassic GS_CapabilityClassic;static kCapabilityFD GS_CapabilityFD;
static kDeviceVersion GS_DeviceVersion;static kBoardInfo ELM_BoardInfo;
bool can_is_open(int channel){return false;}
static bool can_get_termination(int channel,bool *enabled){*enabled=false;return true;}
static unsigned rx_flashes;
void led_flash_RX(int channel){rx_flashes++;}
static uint32_t system_get_flash_addr(uint32_t segment){return segment<8?0x0801C000U:0;}
static uint32_t system_get_timestamp(void){return 1234;}
static bool system_is_option_enabled(int option){return false;}
static void USBD_CtlPrepareRx(USBD_HandleTypeDef *d,uint8_t *p,unsigned length){prepares++;}
static void USBD_CtlSendData(USBD_HandleTypeDef *d,uint8_t *p,unsigned length){sends++;}
''' + setup + r'''
int main(void){
 const uint8_t commands[]={GS_ReqSetHostFormat,GS_ReqSetBitTiming,GS_ReqSetDeviceMode,ELM_ReqWriteFlash};
 const uint16_t lengths[]={4,20,8,0};
 for(unsigned state=0;state<3;state++)for(unsigned i=0;i<4;i++){
  USBD_HandleTypeDef d={.dev_state=state};USBD_SetupReqTypedef r={.bRequestType=0x40,.bRequest=commands[i],.wLength=lengths[i]};
  prepares=sends=0;command_pending=false;memset(ep0_buf,0xA5,sizeof(ep0_buf));
  assert(!control_setup_request(&d,&r));assert(ELM_LastError==FBK_InvalidCommand);
  assert(!prepares && !sends && !command_pending && ep0_buf[0]==0xA5);
 }
 USBD_HandleTypeDef d={.dev_state=USBD_STATE_CONFIGURED};
 USBD_SetupReqTypedef r={.bRequestType=0x40,.bRequest=GS_ReqSetHostFormat,.wLength=4};
 assert(control_setup_request(&d,&r) && prepares==1 && !command_pending);
 r.bRequest=ELM_ReqWriteFlash;r.wLength=0;
 assert(control_setup_request(&d,&r) && command_pending && d.ep0_state==USBD_EP0_DATA_OUT);
 // Read-only enumeration queries remain available before configuration.
 d.dev_state=2;r=(USBD_SetupReqTypedef){.bRequestType=0xC0,.bRequest=GS_ReqGetDeviceVersion,.wLength=12};
 assert(control_setup_request(&d,&r) && sends==1);
 d.dev_state=USBD_STATE_CONFIGURED;r=(USBD_SetupReqTypedef){.bRequestType=0x40,.bRequest=GS_ReqIdentify,.wLength=4};
 assert(!control_setup_request(&d,&r) && ELM_LastError==FBK_UnsupportedFeature);
 assert(!rx_flashes); // USB requests must not masquerade as CAN RX activity.
 return 0;
}
''')

    def test_command_ownership_and_status(self):
        source = ROOT / 'Source/Candlelight/control.c'
        save = function(ROOT / 'Source/system.h', 'static inline uint32_t system_irq_save(')
        restore = function(ROOT / 'Source/system.h', 'static inline void system_irq_restore(')
        cancel = function(source, 'void control_cancel_pending(')
        pending = function(source, 'void control_process_pending(')
        queue = function(source, 'void control_setup_OUT_data(')
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define USBD_STATE_CONFIGURED 1
#define USBD_EP0_DATA_OUT 2
#define FBK_Success 2
#define FBK_InvalidParameter 50
typedef int eFeedback;
typedef struct {uint16_t wLength;} USBD_SetupReqTypedef;
typedef struct {int dev_state,ep0_state,request;} USBD_HandleTypeDef;
static bool command_pending;
static uint32_t command_generation;
static uint8_t ep0_buf[2008],command_buf[2008];
static USBD_SetupReqTypedef command_request,last_setup_request;
static int ELM_LastError,executed,status_sent,stalled;
static int execution_result=FBK_Success;
static bool cancel_during_execution;
static uint32_t primask;
static uint32_t __get_PRIMASK(void){return primask;}
static void __disable_irq(void){primask=1;}
static void __set_PRIMASK(uint32_t s){primask=s;}
static void __DSB(void){}
static void __ISB(void){}
static void USBD_CtlSendStatus(USBD_HandleTypeDef*d){assert(primask);status_sent++;}
static void USBD_CtlError(USBD_HandleTypeDef*d,void*r){assert(primask);stalled++;}
void control_cancel_pending(void);
static eFeedback control_execute_OUT_data(USBD_HandleTypeDef*d){
 assert(!primask);executed++;
 assert(command_request.wLength==4 && command_buf[0]==0x42);
 if(cancel_during_execution)control_cancel_pending();
 return execution_result;
}
''' + save + '\n' + restore + '\n' + cancel + '\n' + pending + '\n' + queue + r'''
int main(void){
 uint32_t outer=system_irq_save(),inner=system_irq_save();
 system_irq_restore(inner);assert(primask==1);system_irq_restore(outer);assert(!primask);
 primask=1;outer=system_irq_save();system_irq_restore(outer);assert(primask==1);primask=0;
 USBD_HandleTypeDef d={.dev_state=USBD_STATE_CONFIGURED,.ep0_state=USBD_EP0_DATA_OUT};
 last_setup_request.wLength=4;ep0_buf[0]=0x42;
 control_setup_OUT_data(&d);assert(command_pending && !executed && !status_sent);
 control_process_pending(&d);assert(executed==1 && status_sent==1 && !stalled && !command_pending);
 control_process_pending(&d);assert(executed==1);
 execution_result=FBK_InvalidParameter;
 control_setup_OUT_data(&d);control_process_pending(&d);
 assert(executed==2 && status_sent==1 && stalled==1 && ELM_LastError==FBK_InvalidParameter);
 control_setup_OUT_data(&d);control_cancel_pending();control_process_pending(&d);assert(executed==2);
 cancel_during_execution=true;execution_result=FBK_Success;
 control_setup_OUT_data(&d);control_process_pending(&d);
 assert(executed==3 && status_sent==1 && stalled==1 && !primask);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
