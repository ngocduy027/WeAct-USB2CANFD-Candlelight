/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "can.h"
#include "led.h"
#include "dfu.h"
#include "control.h"
#include "system.h"
#include "utils.h"
#include "buffer.h"
#include "usb_def.h"
#include "usb_lowlevel.h"
#include "usb_core.h"
#ifdef Candlelight
#include "usb_class.h"
#endif

uint32_t tick_last   = 0;
bool     usb_suspend = false;
bool     blink_leds  = true;

int main(void)
{
    led_init(); // Initialize activity and ready outputs.
    
    if (!system_init())
    {
        system_fault_reset(SYSTEM_FAULT_STARTUP);
    }

    buf_init();
    can_init();
    utils_init();
    control_init(); // AFTER utils_init()
    // Connect USB only after callback-visible state is initialized.
    if (!USBD_Init()) system_fault_reset(SYSTEM_FAULT_STARTUP);
       
    // This loop runs eternally approx 100 times in one millisecond
    int channel = 0;    
    while (true)
    {
        system_watchdog_feed();
        dfu_timer_100ms(HAL_GetTick());
#ifdef Candlelight
        USBD_ProcessLifecycle();
        if (USBD_TrafficReady()) control_process_pending(&GLB_UsbDevice);
        USBD_ProcessOut();
#endif
        if (HAL_PCD_Is_Suspended()) // computer is in sleep mode (USB off)
        {
            led_sleep(); // All controllable LEDs are off during suspend.
            usb_suspend = true;
            __WFI();
            continue;
        }
        else if (usb_suspend)
        {
            usb_suspend = false;            
            blink_leds  = true;
        }

        // blink LED's after power-on and after wake-up from sleep mode
        if (blink_leds)
        {
            blink_leds = false;
            led_blink_power_on(); // Schedule the nonblocking startup indication.
        }
        
        uint32_t tick_now = HAL_GetTick();        
        led_process    (channel, tick_now);
        buf_process    (channel, tick_now);
        control_process(channel, tick_now); // calls error_is_report_due() --> First report the error "Bus Off"
        can_process    (channel, tick_now); // AFTER control!              --> After recover from Bus Off
        
        if (tick_now - tick_last >= 100)
        {
            tick_last = tick_now;            
            can_timer_100ms();

        }
        
        // iterate through all channels if multi-channel board
        channel = (channel + 1) % CHANNEL_COUNT;
    }
}

