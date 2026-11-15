/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "system.h"
#include "can.h"
#include "control.h"

// Linker-reserved user storage is excluded from the complete executable image.
extern uint8_t __user_flash_start[], __user_flash_end[];

uint32_t canfd_clock;
static bool quartz_in_use;
bool system_using_quartz(void) { return quartz_in_use; }
volatile uint32_t timestamp_wrap = 0;
_Static_assert((uint64_t)SYSTEM_TIMESTAMP_TICK_US * 65536U * CAN_MIN_NOMINAL_BITRATE > 160ULL * 1000000U,
               "Classic nominal limit must fit the timestamp epoch");
_Static_assert((uint64_t)SYSTEM_TIMESTAMP_TICK_US * 65536U * CAN_MIN_NOMINAL_BITRATE_FD > 1024ULL * 1000000U,
               "FD nominal limit must fit the timestamp epoch");

void system_init_timestamp();

/*

See "STM32G4 Series - Clock Generation.png" in subfolder "Documentation"

Oscillators:
 +- HSI16 (16 MHz internal RC)
 +- HSE (4–48 MHz external quartz/resonator)
 +- MSI (100 kHz – 48 MHz multi-speed internal)
 +- HSI48 (48 MHz internal, for USB/RNG/CRS)
 +- LSE (32.768 kHz external quartz, RTC)
 +- LSI (~32 kHz internal RC, watchdog/RTC)

PLL Block:
 +- Input: HSI16 / HSE / MSI
 +- PLLM (÷1..16)  --> divides input
 +- PLLN (×8..127) --> multiplies to VCO (64–344 MHz)
 +- Outputs:
     +- PLLR (÷2,4,6,8) --> SYSCLK
     +- PLLQ (÷2,4,6,8) --> USB, SAI, RNG
     +- PLLP (÷2,4,6,8,10,12,14,16, etc.) --> ADC, other peripherals

System Clock (SYSCLK):
 +- Source mux: HSI16 / HSE / MSI / PLLR
 +- Max 170 MHz

AHB Prescaler:
 +- HCLK = SYSCLK ÷ AHB prescaler

APB Prescalers:
 +- PCLK1 = HCLK ÷ APB1 prescaler (max 80 MHz)
 +- PCLK2 = HCLK ÷ APB2 prescaler (max 80 MHz)

Peripheral Clock Domains:
 +- USB:
 |   +- Source mux: HSI48 OR PLLQ (must be exactly 48 MHz)
 |   +- Needs CRS calibration if HSI48 used
 +- FDCAN:
 |   +- Source mux: HSE OR PLLQ OR PCLK1
 +- ADC:
 |   +- Source mux: PLLP OR SYSCLK OR HCLK
 +- UART/I2C/SPI/Timers:
 |   +- Driven from PCLK1 or PCLK2 depending on bus
 +- RTC:
 |   +- Source mux: LSE OR LSI OR HSE/128
 +- Watchdog:
     +- LSI (always)

*/

bool system_init(void)
{
    if (HAL_Init() != HAL_OK)
      return false;

    system_runtime_init();

    // Configure the main internal regulator output voltage
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST) != HAL_OK)
        return false;

    // ------------------------------------------

    RCC_OscInitTypeDef RCC_OscInitStruct  = {0};
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI48; // 48 MHz RC oscillator is always needed as USB clock
    RCC_OscInitStruct.HSI48State          = RCC_HSI48_ON;             // enable HSI (High Speed Internal) 48 MHz RC oscillator

    // HSE_VALUE = QUARTZ_FREQU is assigned in the makefiles
#if HSE_VALUE == 0 // No quartz crystal present on the board

    RCC_OscInitStruct.OscillatorType     |= RCC_OSCILLATORTYPE_HSI; // 16 MHz RC oscillator used as input for PLL if no quartz present
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;             // enable HSI (High Speed Internal) 16 MHz RC oscillator
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;      // use internal oscillator to feed the PLL
    RCC_OscInitStruct.PLL.PLLM            = RCC_PLLM_DIV1;          // divide 16 MHz input clock / 1 --> 16 MHz
    RCC_OscInitStruct.PLL.PLLN            = 20;                     // multiply 16 MHz x 20 --> VCO frequency = 320 MHz (maximum 344 MHz)

#else // quartz crystal is present

    RCC_OscInitStruct.OscillatorType     |= RCC_OSCILLATORTYPE_HSE; // quartz oscillator used as input for PLL
    RCC_OscInitStruct.HSEState            = RCC_HSE_ON;             // enable HSE (High Speed External) quartz oscillator
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSE;      // use external oscillator to feed the PLL
    
    #if HSE_VALUE == 8000000    // 8 MHz quartz 
        RCC_OscInitStruct.PLL.PLLM        = RCC_PLLM_DIV1;          // divide 8 MHz input clock / 1 --> 8 MHz
        RCC_OscInitStruct.PLL.PLLN        = 40;                     // multiply 8 MHz x 40 --> VCO frequency = 320 MHz (maximum 344 MHz)
    #elif HSE_VALUE == 16000000 // 16 MHz quartz 
        RCC_OscInitStruct.PLL.PLLM        = RCC_PLLM_DIV1;          // divide 16 MHz input clock / 1 --> 16 MHz
        RCC_OscInitStruct.PLL.PLLN        = 20;                     // multiply 16 MHz x 20 --> VCO frequency = 320 MHz (maximum 344 MHz)    
    #elif HSE_VALUE == 25000000 // 25 MHz quartz 
        RCC_OscInitStruct.PLL.PLLM        = RCC_PLLM_DIV5;          // divide 25 MHz input clock / 5 --> 5 MHz
        RCC_OscInitStruct.PLL.PLLN        = 64;                     // multiply 5 MHz x 64 --> VCO frequency = 320 MHz (maximum 344 MHz)
    #else
        #error "Quartz frequency not implemented in system.c"
    #endif
#endif    

    // ------------------------------------------

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLP     = RCC_PLLP_DIV2; // PLL output P = VCO / 2 = 160 MHz (for ADC)
    RCC_OscInitStruct.PLL.PLLQ     = RCC_PLLQ_DIV2; // PLL output Q = VCO / 2 = 160 MHz (for FDCAN)
    RCC_OscInitStruct.PLL.PLLR     = RCC_PLLR_DIV2; // PLL output R = VCO / 2 = 160 MHz (for SYSCLK)
    quartz_in_use = RCC_OscInitStruct.PLL.PLLSource == RCC_PLLSOURCE_HSE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        // A failed crystal must still allow USB diagnosis/recovery. CAN precision
        // is reduced on HSI16; report the actual source through BoardFlags.
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSI48;
        RCC_OscInitStruct.HSIState = RCC_HSI_ON;
        RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
        RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
        RCC_OscInitStruct.PLL.PLLN = 20;
        if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) return false;
        quartz_in_use = false;
    }
    if (quartz_in_use) HAL_RCC_EnableCSS();

    // FDCAN time-quanta clock must not exceed its APB clock (RM0440 44.3.3).
    // G4 boost mode supports these buses at 160 MHz. TIM3 remains at 160 MHz.
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; // set SYSCLK = PLL R   = 160 MHz
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;         // set HCLK   = SYSCLK  = 160 MHz (AHB  = Advanced High-performance Bus)
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;           // PCLK1 = 160 MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;           // set PCLK2  = HCLK /2 =  80 MHz (APB2 = Advanced Peripheral Bus 2)
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_8) != HAL_OK)
        return false;

    // Initializes the peripherals clocks
    RCC_PeriphCLKInitTypeDef RCC_PeriphClkInit = {0};
    RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_FDCAN;
    // FDCAN uses PLL output Q (160 MHz)
    RCC_PeriphClkInit.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PLL;
    // Use internal 48 MHz oscillator because 48 MHz for USB cannot be derived from 320 MHz PLL clock with dividers 1,2,4,8
    RCC_PeriphClkInit.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInit) != HAL_OK)
        return false;

    // Clock Recovery System (CRS) calibrates the internal HSI48 oscillator so it stays accurate enough for USB.
    // The USB SOF (Start-Of-Frame) packets, which arrive every 1 ms, are used for synchronization.
    __HAL_RCC_CRS_CLK_ENABLE();
    RCC_CRSInitTypeDef pInit = {0};
    pInit.Prescaler   = RCC_CRS_SYNC_DIV1;
    pInit.Source      = RCC_CRS_SYNC_SOURCE_USB;
    pInit.Polarity    = RCC_CRS_SYNC_POLARITY_RISING;
    pInit.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000); // 48000 clock cycles in 1 ms
    pInit.ErrorLimitValue       = 34;  // tolerance window for frequency error
    pInit.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;

    HAL_RCCEx_CRSConfig(&pInit);

    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE(); // just nrst is on port G

    canfd_clock = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN); // 160 MHz

    system_init_timestamp();
    // Preserve board option bytes; firmware startup must not program nonvolatile state.
    return true;
}

// While TARGET_MCU (from the make file) defines for which MCU serie the code was COMPILED,
// this function returns on which MCU the code is actually RUNNING.
// The user may have uploaded the firmware to the wrong processor.
// This function can be used to prove the we run on the expected processor.
eMcuSerie system_get_mcu_serie()
{
    // HAL_GetDEVID() reads a 12 bit identifier (DBG_IDCODE) that is unique for each processor family.
    switch (HAL_GetDEVID())
    {
        case 0x460: // STM32G071 + G081
        case 0x465: // STM32G051 + G061
        case 0x466: // STM32G031 + G041
        case 0x467: // STM32G0B1 + G0C1
            return SERIE_G0;

        case 0x468: // STM32G431 + G441
        case 0x469: // STM32G471 + G473 + G474 + G483 + G484
        case 0x479: // STM32G491 + G4A1
            return SERIE_G4;

        default: // processor serie not implemented
            return SERIE_Unknown;
    }
}

// 160 MHz
uint32_t system_get_can_clock()
{
    return canfd_clock;
}

// Free-running CAN capture timer; returned timestamps use microsecond units.
void system_init_timestamp()
{
    // TIM3 runs at 160 MHz: PCLK1 directly or the APB timer multiplier.
    // Its update interrupt provides the epoch even while CAN is closed.
    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM3->CR1   = 0;
    TIM3->CR2   = 0;
    TIM3->SMCR  = 0;
    TIM3->DIER  = 0;
    TIM3->CCMR1 = 0;
    TIM3->CCMR2 = 0;
    TIM3->CCER  = 0;
    TIM3->PSC   = (SystemCoreClock / 1000000) * SYSTEM_TIMESTAMP_TICK_US - 1;
    TIM3->ARR   = 0xFFFF;
    TIM3->EGR   = TIM_EGR_UG;
    TIM3->SR    = 0; // Discard the prescaler-loading software update.
    timestamp_wrap = 0;
    TIM3->DIER  = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    TIM3->CR1  |= TIM_CR1_CEN;
    
#ifdef Candlelight
    // SLCAN's CDC buffer is main-loop owned; retain its polled CAN servicing.
    HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
#if CHANNEL_COUNT > 1
    HAL_NVIC_SetPriority(FDCAN2_IT0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(FDCAN2_IT0_IRQn);
#endif
#if CHANNEL_COUNT > 2
    HAL_NVIC_SetPriority(FDCAN3_IT0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(FDCAN3_IT0_IRQn);
#endif
#endif // Candlelight
}

void FDCAN1_IT0_IRQHandler(void) { can_interrupt(0); }
#if CHANNEL_COUNT > 1
void FDCAN2_IT0_IRQHandler(void) { can_interrupt(1); }
#endif
#if CHANNEL_COUNT > 2
void FDCAN3_IT0_IRQHandler(void) { can_interrupt(2); }
#endif

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF)
    {
        TIM3->SR = ~TIM_SR_UIF;
        ++timestamp_wrap;
    }
}

// Read the timer and epoch coherently, including an update not yet serviced.
uint32_t system_get_timestamp(void)
{
    uint32_t mask = system_irq_save();
    uint32_t high = timestamp_wrap;
    uint32_t low = TIM3->CNT;
    if (TIM3->SR & TIM_SR_UIF)
    {
        ++high;
        low = TIM3->CNT;
    }
    system_irq_restore(mask);
    return ((high << 16) | low) * SYSTEM_TIMESTAMP_TICK_US;
}

uint32_t system_get_timewrap(void)
{
    return system_get_timestamp() >> 16;
}

// Extend captures within one 65.536 ms epoch of SOF. Minimum nominal rates
// bound classic frames to 32 ms (160 bits) and FD to 51.2 ms (1024 bits).
// Data timing cannot be slower than nominal when FD starts. IRQ draining
// avoids foreground delays; flash writes require every channel closed.
// Both capture resolution and wire units are one microsecond.
uint32_t system_extend_timestamp(uint16_t captured)
{
    uint32_t now = system_get_timestamp();
    uint16_t elapsed = (uint16_t)((uint16_t)(now / SYSTEM_TIMESTAMP_TICK_US) - captured);
    return now - (uint32_t)elapsed * SYSTEM_TIMESTAMP_TICK_US;
}

// ----------------------------- Option Bytes ----------------------------------

// returns true if the requested option is set in the Option Bytes
bool system_is_option_enabled(eOptionBytes e_Option)
{
    // Get option bytes
    FLASH_OBProgramInitTypeDef cur_values = {0};
    HAL_FLASHEx_OBGetConfig(&cur_values);

    switch (e_Option)
    {
        case OPT_BOR_Level4:    return (cur_values.USERConfig & FLASH_OPTR_BOR_LEV_Msk)  == OB_BOR_LEVEL_4;
        case OPT_BOOT0_Enable:  return (cur_values.USERConfig & FLASH_OPTR_nSWBOOT0_Msk) == OB_BOOT0_FROM_PIN;
        case OPT_BOOT0_Disable: return (cur_values.USERConfig & FLASH_OPTR_nSWBOOT0_Msk) == OB_BOOT0_FROM_OB;
    }
    return false;
}

// Set BoR (Brown-Out Reset) level to 4 (2.8 Volt = highet value)
// This means that a reset is generated when power voltage falls below 2.8V.
// This eliminates an issue where poor quality USB hubs that provide low voltage before switching the 5 Volt supply on
// which was causing PoR issues where the microcontroller would enter boot mode incorrectly.
// ----------------
// This function can also define if the pin BOOT0 is ignored.
// This pin is STUPIDLY the same as the CAN RX pin which really sucks.
// By only restarting the computer the CANable goes into Bootloader mode.
// Thefore this firmware gives the user the possibility to ignore pin BOOT0.
// Read the detailed description here: https://netcult.ch/elmue/CANable Firmware Update
// ====================================================================================================
// IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT
// If you modify this code and introduce a bug you may end up in a frozen firmware that cannot be updated anymore!
// ====================================================================================================
eFeedback system_set_option_bytes(eOptionBytes e_Option)
{
#ifdef WeActStudio
    // Preserve the board's documented BOOT0 button and factory option bytes.
    (void)e_Option;
    return FBK_UnsupportedFeature;
#endif
    // IMPORTANT:
    // The user may have uploaded the firmware to the wrong processor.
    // The serie STM32G0XX has different bits in the OPTR register than the serie STM32G4XX.
    // It is VERY important not to execute the following code on the wrong processor!
    // Screwing up the Option Bytes may have fatal consquences that can make the board unusable.
    if (system_get_mcu_serie() != SERIE_G4)
        return FBK_UnsupportedFeature;

    if (can_is_any_open())
        return FBK_AdapterMustBeClosed;

    if (system_is_option_enabled(e_Option))
        return FBK_Success; // nothing to do

    // The following bits apply only to the STM32G4XX serie:
    // OPTR bit 26 nSWBOOT0 == 1 --> pin BOOT0 is enabled
    // OPTR bit 26 nSWBOOT0 == 0 --> pin BOOT0 is disabled, bit nBOOT0 defines boot mode
    // OPTR bit 27 nBOOT0   == 1 --> boot into main flash memory
    // OPTR bit 27 nBOOT0   == 0 --> nBOOT1 defines boot mode
    // OPTR bit 23 nBOOT1   == 1 --> boot into bootloader (System)
    // OPTR bit 23 nBOOT1   == 0 --> boot into SRAM1
    // By default the register OPTR has the value 0xFFEFFCXX
    // After disabling the pin BOOT0 it will have 0xFBEFFCXX
    FLASH_OBProgramInitTypeDef prog_values = {0};
    switch (e_Option)
    {
        case OPT_BOR_Level4: // set level = 2.8 Volt
            prog_values.OptionType = OPTIONBYTE_USER;
            prog_values.USERType   = OB_USER_BOR_LEV;
            prog_values.USERConfig = OB_BOR_LEVEL_4;
            break;
        case OPT_BOOT0_Enable: // pin BOOT0 defines boot mode (bootloader of flash memory)
            prog_values.OptionType = OPTIONBYTE_USER;
            prog_values.USERType   = OB_USER_nSWBOOT0  | OB_USER_nBOOT0 | OB_USER_nBOOT1;  // 0x00006200
            prog_values.USERConfig = OB_BOOT0_FROM_PIN | OB_nBOOT0_SET  | OB_BOOT1_SYSTEM; // 0x0C800000
            break;
        case OPT_BOOT0_Disable: // Option Byte bits nBOOT0 and nBOOT1 define boot mode
            prog_values.OptionType = OPTIONBYTE_USER;
            prog_values.USERType   = OB_USER_nSWBOOT0  | OB_USER_nBOOT0 | OB_USER_nBOOT1;  // 0x00006200
            prog_values.USERConfig = OB_BOOT0_FROM_OB  | OB_nBOOT0_SET  | OB_BOOT1_SYSTEM; // 0x08800000
            break;
        default:
            return FBK_InvalidParameter;
    }

    // The following flash programming procedure takes approx 25 ms.

    // IMPORTANT:
    // If previous errors are not cleared, HAL_FLASHEx_OBProgram() will fail.
    // This was wrong in all legacy firmware versions. (fixed by ElmüSoft)
    // The programmers did not even notice this bug because of a non-existent error handling (sloppy code).
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    // All the following functions return either HAL_OK or HAL_ERROR

    if (HAL_FLASH_Unlock() != HAL_OK) return FBK_OptBytesProgrFailed;
    if (HAL_FLASH_OB_Unlock() != HAL_OK)
    {
        HAL_FLASH_Lock();
        return FBK_OptBytesProgrFailed;
    }

    bool b_OK1 = HAL_FLASHEx_OBProgram(&prog_values) == HAL_OK; // Program option bytes

    // always lock, even if programming should have failed
    bool b_OK2 = HAL_FLASH_OB_Lock() == HAL_OK; // Lock option bytes
    bool b_OK3 = HAL_FLASH_Lock()    == HAL_OK; // Lock flash
    if (!b_OK1 || !b_OK2 || !b_OK3)
        return FBK_OptBytesProgrFailed;

    // NOTE:
    // The function HAL_FLASH_OB_Launch() does not work here to activate the new Option Bytes.
    // Even if the pin BOOT0 has been enabled, the pin will have no effect until a hardware reset is executed.
    // Therefore dfu_switch_to_bootloader() handles this special case.
    return FBK_Success;
}

// ----------------------------- R/W Flash Memory ----------------------------------

// The linker reserves eight 2 KB pages in the first 128 KB, independently of
// the device flash-size register and any larger G473 bank configuration.
// Neither application sections nor their .data load image may overlap them.

// Get the start address of the segment (segment 0 is the last segment at the end of the flash memory)
// returns 0 if segment is invalid or occupied by firmware.
uint32_t system_get_flash_addr(uint32_t segment)
{
    uintptr_t start = (uintptr_t)__user_flash_start;
    uintptr_t end = (uintptr_t)__user_flash_end;
    uint32_t pages = (uint32_t)((end - start) / FLASH_PAGE_SIZE);
    if (segment >= pages) return 0;
    return (uint32_t)(end - (segment + 1U) * FLASH_PAGE_SIZE);
}

// Erase a flash segment and write user data to it (this takes approx. 22 ms)
// The first 2 bytes in the segment store the data length.
// If the same data is already stored in the flash segment, the function does nothing.
// This avoids wearing off the flash memory.
// The buffer will be modified here and the buffer must have MAX_FLASH_DATA_LEN + 2 bytes!
eFeedback system_write_flash(uint32_t segment, uint8_t* buffer, uint16_t data_len)
{
    if (can_is_any_open()) return FBK_AdapterMustBeClosed;
    uint32_t flash_addr = system_get_flash_addr(segment);
    if (flash_addr == 0 || data_len > MAX_FLASH_DATA_LEN)
        return FBK_ParamOutOfRange;
    
    // Avoid wearing off the flash memory by useless erase and write operations.
    uint32_t cur_len = ((uint16_t*)flash_addr)[0];
    if (cur_len == 0xFFFF) cur_len = 0; // empty segment
    if (cur_len == data_len && memcmp((uint8_t*)(flash_addr + 2), buffer, data_len) == 0)
        return FBK_Success;
       
    // Clear pending flash errors
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    if (HAL_FLASH_Unlock() != HAL_OK) // Unlock flash
        return FBK_ErrorFromHAL;

    FLASH_EraseInitTypeDef erase_init;
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Page      = (flash_addr - FLASH_BASE) / FLASH_PAGE_SIZE;
    erase_init.Banks     = FLASH_BANK_1;
    erase_init.NbPages   = 1;

    // Erase entire 2 kB flash segment.
    uint32_t page_error;
    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK) 
    {
        HAL_FLASH_Lock();
        return FBK_ErrorFromHAL;
    }
    
    // If the user passes a length of zero --> only erase the flash segment (entirely FF FF FF ...)
    if (data_len > 0)
    {
        // Move data two bytes up and store the length (2 bytes) before the data.
        memmove(buffer + 2, buffer, data_len);
        memcpy(buffer, &data_len, sizeof(data_len));
        data_len += 2;        
        
        // The HAL writes 64 bit double words to the flash memory
        uint16_t  len_64 = (data_len + 7) / 8;

        for (uint16_t i=0; i<len_64; i++, flash_addr += 8) 
        {
            uint64_t word = UINT64_MAX;
            uint16_t offset = i * 8U;
            uint16_t remaining = data_len - offset;
            memcpy(&word, buffer + offset, remaining < 8U ? remaining : 8U);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, flash_addr, word) != HAL_OK)
            {
                HAL_FLASH_Lock();
                return FBK_ErrorFromHAL;
            }
        }
    }

    return HAL_FLASH_Lock() == HAL_OK ? FBK_Success : FBK_ErrorFromHAL;
}