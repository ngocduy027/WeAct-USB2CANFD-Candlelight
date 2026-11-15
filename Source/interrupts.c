/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "interrupts.h"
#include "can.h"
#include "led.h"
#include "system.h"

extern PCD_HandleTypeDef hpcd_USB_FS;

void NMI_Handler(void)
{
    system_fault_reset((RCC->CIFR & RCC_CIFR_CSSF) ? SYSTEM_FAULT_CLOCK : SYSTEM_FAULT_NMI);
}

void HardFault_Handler(void) { system_fault_reset(SYSTEM_FAULT_HARD); }
void MemManage_Handler(void) { system_fault_reset(SYSTEM_FAULT_MEMORY); }
void BusFault_Handler(void) { system_fault_reset(SYSTEM_FAULT_BUS); }
void UsageFault_Handler(void) { system_fault_reset(SYSTEM_FAULT_USAGE); }

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

// Handle USB interrupts
void USB_LP_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_FS);
}

// Handle USB interrupts
void USB_HP_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_FS);
}

// Handle SysTick interrupt
void SysTick_Handler(void)
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}

