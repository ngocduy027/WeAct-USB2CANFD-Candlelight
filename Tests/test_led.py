"""Execute LED GPIO/timing policy with real readiness and strict output checks."""
import re
import unittest
from test_regressions import ROOT, function, run_c


def fixture(activity_only):
    header=(ROOT/'Source/led.h').read_text()
    header='\n'.join(line for line in header.splitlines() if not line.startswith(('#include','#pragma')))
    source=(ROOT/'Source/led.c').read_text()
    source=re.sub(r'^#include .*$', '', source, flags=re.M)
    can=ROOT/'Source/can.c'
    readiness=function(can,'bool can_is_ready(')
    opened=function(can,'bool can_is_open(')
    return ('#define LED_CAN_ACTIVITY_ONLY\n' if activity_only else '')+r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define CHANNEL_COUNT 1
#define LED_MODE 1
#define GPIO_NOPULL 0
#define GPIO_SPEED_FREQ_LOW 0
#define LED_ON 0
#define LED_OFF 1
#define GPIO_PIN_0 1
#define GPIO_PIN_1 2
#define GPIO_PIN_2 4
typedef struct {unsigned ODR;} GPIO_TypeDef;
typedef struct {unsigned Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
static GPIO_TypeDef port;
#define GPIOA (&port)
#define LED_TX_PORTS GPIOA
#define LED_RX_PORTS GPIOA
#define LED_RDY_PORTS GPIOA
#define LED_TX_PINS GPIO_PIN_1
#define LED_RX_PINS GPIO_PIN_0
#define LED_RDY_PINS GPIO_PIN_2
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
static unsigned tick;
static uint32_t HAL_GetTick(void){return tick;}
static void HAL_GPIO_WritePin(GPIO_TypeDef *p,unsigned pin,unsigned value){if(value)p->ODR|=pin;else p->ODR&=~pin;}
static void HAL_GPIO_Init(GPIO_TypeDef *p,GPIO_InitTypeDef *c){
#ifdef LED_CAN_ACTIVITY_ONLY
 assert(p->ODR&c->Pin); // Off was preloaded before enabling an active-low GPIO.
#endif
}
#define HAL_FDCAN_STATE_BUSY 2
#define HAL_FDCAN_STATE_ERROR 3
typedef struct {bool is_open,suspended,recovery_needed,recover_bus_off;struct {unsigned State;} handle;struct {unsigned BusOff;} cur_status;} can_class;
static can_class can_inst[1];
''' + readiness+'\n'+opened+r'''
#ifdef LED_CAN_ACTIVITY_ONLY
static uint32_t mask_state;
static uint32_t system_irq_save(void){unsigned old=mask_state;mask_state=1;return old;}
static void system_irq_restore(uint32_t old){mask_state=old;}
static bool usb_ready=true;
static bool USBD_TrafficReady(void){return usb_ready;}
#else
#define BUS_StatusOff 3
static struct {unsigned bus_status,app_flags;} error;
#define error_get_state(c) (&error)
static bool can_is_any_open(void){return can_is_open(0);}
#endif
''' + header+'\n'+source


class LedPolicy(unittest.TestCase):
    def test_weact_traffic_ready_close_suspend_and_tick_wrap(self):
        run_c(fixture(True)+r'''
int main(void){
 led_init();assert(port.ODR==7);led_process(0,0);assert(port.ODR==7);
 led_blink_power_on();led_blink_identify(0,true);led_process(0,100);assert(port.ODR==7);
 can_inst[0].is_open=true;can_inst[0].handle.State=HAL_FDCAN_STATE_BUSY;
 assert(can_is_ready(0));led_process(0,0);assert(port.ODR==3);
 // The first real message at HAL tick zero still gets a bounded pulse.
 led_flash_TX(0);assert(port.ODR==1 && led_inst[0].TX_active);
 tick=14;led_process(0,0xFFFFFFFFU);assert(port.ODR==1);
 tick=15;led_process(0,0);assert(port.ODR==3 && !led_inst[0].TX_active);
 tick=54;led_flash_TX(0);assert(port.ODR==3);
 tick=55;led_flash_TX(0);assert(port.ODR==1);
 tick=70;led_flash_RX(0);led_process(0,69);assert(port.ODR==2);
 // Neither a close nor a suspend may leave activity or READY latched on.
 can_inst[0].is_open=false;led_turn_TX(0,true);assert(port.ODR==7);
 can_inst[0].is_open=true;led_process(0,tick);assert(port.ODR==3);
 led_sleep();assert(port.ODR==7 && !led_inst[0].RX_active && !led_inst[0].TX_active);
 usb_ready=false;led_process(0,tick);led_flash_RX(0);led_flash_TX(0);assert(port.ODR==7);
 usb_ready=true;
 // Logical open includes suspended/recovering sessions, while READY must not.
 can_inst[0].suspended=true;assert(can_is_open(0) && !can_is_ready(0));led_process(0,tick);assert(port.ODR==7);
 can_inst[0].suspended=false;can_inst[0].recovery_needed=true;assert(!can_is_ready(0));
 can_inst[0].recovery_needed=false;can_inst[0].recover_bus_off=true;assert(!can_is_ready(0));
 can_inst[0].recover_bus_off=false;can_inst[0].cur_status.BusOff=1;assert(!can_is_ready(0));led_process(0,tick);assert(port.ODR==7);
 can_inst[0].cur_status.BusOff=0;can_inst[0].handle.State=HAL_FDCAN_STATE_ERROR;assert(!can_is_ready(0));
 can_inst[0].handle.State=HAL_FDCAN_STATE_BUSY;assert(can_is_ready(0));
 tick=0xFFFFFFFAU;led_sleep();led_process(0,tick);led_flash_RX(0);assert(port.ODR==2);
 tick=8;led_process(0,0);assert(port.ODR==2);
 tick=9;led_process(0,0);assert(port.ODR==3);
 mask_state=1;tick=49;led_flash_RX(0);led_process(0,tick);assert(mask_state==1 && port.ODR==2);
 return 0;
}
''')

    def test_other_boards_keep_existing_status_and_identify_behavior(self):
        run_c(fixture(False)+r'''
int main(void){
 led_init();assert(port.ODR==0);
 led_process(0,0);assert((port.ODR&6)==4); // Closed: old TX-on/READY-off policy.
 SetRxLed(0,false); // Simulate the completed legacy startup pattern.
 can_inst[0].is_open=true;can_inst[0].handle.State=HAL_FDCAN_STATE_BUSY;
 led_turn_TX(0,false);led_process(0,1);assert(port.ODR==3);
 led_blink_identify(0,true);assert((port.ODR&3)==0 && led_inst[0].identify);
 led_sleep();assert(port.ODR==7);
 return 0;
}
''')

    def test_same_day_revision_keeps_usb_year_month(self):
        source=(ROOT/'Source/Candlelight/usb_class.c').read_text(encoding='cp1252')
        macro=next(line for line in source.splitlines() if line.startswith('#define USB_FIRMWARE_DATE_BCD'))
        for version in ('0x26111501U','0x261115U'):
            run_c('#include <stdint.h>\n#include <assert.h>\n#define FIRMWARE_VERSION_BCD '+version+'\n'+macro+r'''
int main(void){assert(USB_FIRMWARE_DATE_BCD==0x261115U);assert(((USB_FIRMWARE_DATE_BCD>>8)&0xFFFF)==0x2611);return 0;}
''')


if __name__=='__main__':unittest.main()
