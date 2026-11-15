/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "can.h"
#include "error.h"
#include "led.h"
#include "utils.h"
#include "control.h"
#include "buffer.h"
#include "system.h"
#ifdef Candlelight
#include "usb_class.h"
#endif

#define SECOND_SAMPL_POINT_PERCENT     50  // Secondary Sample Point at 50% of data bit for TDC compensation
#define CAN_TX_TIMEOUT                500  // after 500 ms cancel pending Tx requests --> clear FIFO and packet buffer

// ----- Globals
eUserFlags           GLB_UserFlags[CHANNEL_COUNT];

// ---- Settings  (from settings.h)
FDCAN_GlobalTypeDef* SET_CanInterfaces[CHANNEL_COUNT] = { CAN_INTERFACES };
GPIO_TypeDef*        SET_CanPorts     [CHANNEL_COUNT] = { CAN_PORTS };
int                  SET_CanPins      [CHANNEL_COUNT] = { CAN_PINS  };
uint8_t              SET_CanAlternates[CHANNEL_COUNT] = { CAN_ALTERNATES };
GPIO_TypeDef*        SET_TermPorts    [CHANNEL_COUNT] = { TERMINATOR_PORTS };
int                  SET_TermPins     [CHANNEL_COUNT] = { TERMINATOR_PINS  };
GPIO_TypeDef*        SET_CanRxEnPorts [CHANNEL_COUNT] = { CAN_RX_EN_PORTS };
int                  SET_CanRxEnPins  [CHANNEL_COUNT] = { CAN_RX_EN_PINS };


// ----- Class Instance
can_class            can_inst[CHANNEL_COUNT] = {0};

// ----- Private Methods
void      can_reset(int channel);
static void can_service_io(int channel);
static bool can_cancel_hardware(int channel);
static void can_stop_hardware(int channel);
static void can_sample_status(int channel);
void      can_print_info(int channel);
bool      can_apply_filters(can_class* inst);
uint32_t  can_calc_bit_count_in_frame(can_class* inst, FDCAN_RxHeaderTypeDef *header);

// Initialize CAN peripheral settings, but don't actually start the peripheral
// called from the main loop
void can_init()
{
    __HAL_RCC_FDCAN_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct;
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        can_reset(C);

        if (SET_TermPins[C] >= 0)
        {
            GPIO_InitStruct.Pin       = SET_TermPins[C];
            GPIO_InitStruct.Mode      = TERMINATOR_MODE;
            GPIO_InitStruct.Alternate = 0;
            GPIO_InitStruct.Pull      = GPIO_NOPULL;
            GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
            HAL_GPIO_Init(SET_TermPorts[C], &GPIO_InitStruct);
        }

        if (SET_CanRxEnPins[C] >= 0)
        {
            GPIO_InitStruct.Pin       = SET_CanRxEnPins[C];
            GPIO_InitStruct.Mode      = CAN_RX_EN_MODE;
            GPIO_InitStruct.Alternate = 0;
            GPIO_InitStruct.Pull      = GPIO_NOPULL;
            GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
            HAL_GPIO_Init(SET_CanRxEnPorts[C], &GPIO_InitStruct);
            HAL_GPIO_WritePin(SET_CanRxEnPorts[C], SET_CanRxEnPins[C], CAN_RX_EN_ON);
        }

        GPIO_InitStruct.Pin       = SET_CanPins[C];
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;   // AF = alternate function
        GPIO_InitStruct.Alternate = SET_CanAlternates[C];  // switch pin multiplexer to CAN instance
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(SET_CanPorts[C], &GPIO_InitStruct);

        can_inst[C].handle.Instance = SET_CanInterfaces[C];
    }
}

// called from init() and close() --> reset variable for the next can_open().
// reset only variables here that cannot be reset in can_open()
void can_reset(int channel)
{
    can_class* inst = &can_inst[channel];

#ifdef Slcan
    inst->bitrate_nominal.Brp = 0;
    inst->bitrate_data.Brp = 0;
#endif
    inst->fd_enabled = false;
    inst->suspended = false;
    inst->recovery_needed = false;

    inst->std_filter_count = 0;
    inst->ext_filter_count = 0;
    inst->busload_interval = 0;
    inst->bit_count_total = 0;
    inst->busload_counter = 0;
    inst->old_busload_pct = 0;
    inst->busload_start_tick = HAL_GetTick();
    inst->tx_pending       = 0;
    inst->is_open          = false;

    // this is indispensable here, otherwise Slcan is dead after a Tx buffer overlow and closing the adapter.
    buf_clear_can_buffer(channel);
}

// Start the FDCAN instance
// mode = FDCAN_MODE_NORMAL, FDCAN_MODE_BUS_MONITORING, FDCAN_MODE_INTERNAL_LOOPBACK, FDCAN_MODE_EXTERNAL_LOOPBACK
// ATTENTION: Set all GLB_UserFlags before opening
eFeedback can_open(int channel, uint32_t mode)
{
    can_class* inst = &can_inst[channel];
    if (can_is_open(channel) && !inst->reinitializing)
        return FBK_AdapterMustBeClosed; // already open

    // Nominal baudrate is mandatory
    if (inst->bitrate_nominal.Brp == 0)
        return FBK_BaudrateNotSet;
    // Also cover preset/direct opens, including shared SLCAN paths.
    uint32_t minimum = can_using_FD(channel) ? CAN_MIN_NOMINAL_BITRATE_FD : CAN_MIN_NOMINAL_BITRATE;
    if (can_calc_baud(&inst->bitrate_nominal) < minimum)
        return FBK_ParamOutOfRange;

    bool other_open = false;
    for (int C = 0; C < CHANNEL_COUNT; ++C)
        if (C != channel && can_is_open(C)) other_open = true;
    if (!other_open)
    {
        // Reset all CAN instances FDCAN1, FDCAN2, FDCAN3 only if no channel is open
        __HAL_RCC_FDCAN_FORCE_RESET();
        __HAL_RCC_FDCAN_RELEASE_RESET();
    }

    if (!inst->reinitializing) error_init(channel);
    led_blink_identify(channel, false); // stop identify blinking

    FDCAN_InitTypeDef* init = &inst->handle.Init;
    init->ClockDivider          = FDCAN_CLOCK_DIV1;
    init->Mode                  = mode;
    init->AutoRetransmission    = (GLB_UserFlags[channel] & USR_Retransmit) ? ENABLE : DISABLE;
    init->TransmitPause         = DISABLE;
    init->ProtocolException     = ENABLE;
    init->TxFifoQueueMode       = FDCAN_TX_FIFO_OPERATION;
    init->StdFiltersNbr         = inst->std_filter_count;
    init->ExtFiltersNbr         = inst->ext_filter_count;

    // ------------------- baudrate ------------------------

    init->FrameFormat           = FDCAN_FRAME_CLASSIC;
    init->NominalPrescaler      = inst->bitrate_nominal.Brp;
    init->NominalTimeSeg1       = inst->bitrate_nominal.Seg1;
    init->NominalTimeSeg2       = inst->bitrate_nominal.Seg2;
    init->NominalSyncJumpWidth  = inst->bitrate_nominal.Sjw;

    // The host START flag selects FD explicitly. BRS is selected per frame;
    // equal nominal/data timings remain valid for an FD frame requesting BRS.
    // WeAct's transceiver data rate is limited to 5 Mbit/s.
    if (can_using_FD(channel))
    {
        init->FrameFormat       = FDCAN_FRAME_FD_BRS; // Honor each frame's requested BRS bit, including equal bitrates.
        init->DataPrescaler     = inst->bitrate_data.Brp;
        init->DataTimeSeg1      = inst->bitrate_data.Seg1;
        init->DataTimeSeg2      = inst->bitrate_data.Seg2;
        init->DataSyncJumpWidth = inst->bitrate_data.Sjw;
    }

    // ------------------ init bus load ------------------------

    // Calculate the length of 1 nominal CAN bus bit in nanoseconds (500 kBaud --> nom_bit_len_ns = 2000)

    uint32_t clock_MHz = system_get_can_clock() / 1000000; // 160
    inst->nom_bit_len_ns  = 1 + inst->bitrate_nominal.Seg1 + inst->bitrate_nominal.Seg2; // time quantums
    inst->nom_bit_len_ns *= inst->bitrate_nominal.Brp;     // clock prescaler
    inst->nom_bit_len_ns *= 1000;                          // µs -> ns
    inst->nom_bit_len_ns /= clock_MHz;

    inst->bit_count_total = 0;
    inst->old_busload_pct = 0;
    inst->busload_start_tick = HAL_GetTick();
    inst->bitrate_printed_once = false;
    inst->delay_printed_once   = false;
    inst->recover_bus_off      = false;

    // ------------------ Init FDCAN ----------------------

    // sets inst->handle.State == HAL_FDCAN_STATE_READY
    if (HAL_FDCAN_Init(&inst->handle) != HAL_OK)
        goto fail; // error detail in inst->handle.ErrorCode

    // ---------------- Rx/Tx Timestamps ------------------

    if (HAL_FDCAN_ConfigTimestampCounter(&inst->handle, FDCAN_TIMESTAMP_PRESC_1)  != HAL_OK || // use no prescaler
        HAL_FDCAN_EnableTimestampCounter(&inst->handle, FDCAN_TIMESTAMP_EXTERNAL) != HAL_OK || // use timer TIM3 (see system.c)
        HAL_FDCAN_ConfigInterruptLines(&inst->handle,
            FDCAN_IT_GROUP_RX_FIFO0 | FDCAN_IT_GROUP_RX_FIFO1 | FDCAN_IT_GROUP_SMSG |
            FDCAN_IT_GROUP_TX_FIFO_ERROR | FDCAN_IT_GROUP_BIT_LINE_ERROR |
            FDCAN_IT_GROUP_PROTOCOL_ERROR, FDCAN_INTERRUPT_LINE0) != HAL_OK ||
        HAL_FDCAN_ActivateNotification(&inst->handle,
            FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO1_NEW_MESSAGE |
            FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_RX_FIFO1_MESSAGE_LOST |
            FDCAN_IT_TX_EVT_FIFO_NEW_DATA | FDCAN_IT_TX_EVT_FIFO_ELT_LOST |
            FDCAN_IT_TX_COMPLETE | FDCAN_IT_TX_ABORT_COMPLETE |
            FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_BUS_OFF |
            FDCAN_IT_ARB_PROTOCOL_ERROR | FDCAN_IT_DATA_PROTOCOL_ERROR,
            FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2) != HAL_OK)
        goto fail; // error detail in inst->handle.ErrorCode

    // ---------------- TDC compensation ------------------

    if (HAL_FDCAN_DisableTxDelayCompensation(&inst->handle) != HAL_OK) goto fail;

    inst->tdc_offset = 0;
    if (can_using_FD(channel))
    {
        // The transceiver Delay Compensation (TDC) compensates for the delay between the CAN Tx pin and the CAN Rx pin of the processor.
        // The processor measures the delay of the transceiver chip while a CAN FD packet with BRS is sent to CAN bus.
        // The secondary samplepoint (SSP) does not need to be the same as the primary samplepoint specified in can_bitrate_data.
        // The SSP is only used to verify that the data bits are sent without error to CAN bus.
        // Here a fix SSP at 50% of the length of a data bit is calculated.
        // The SSP offset is measured in mtq (minimum time quantums = one period of 160 MHz)

        // Calculate the length of one data bit in 'mtq'.
        uint32_t data_bit_len = inst->bitrate_data.Brp * (1 + inst->bitrate_data.Seg1 + inst->bitrate_data.Seg2);

        // Calculate the offset of the SSP in 'mtq' at 50% of the data bit.
        // The secondary samplepoint is not critical. SECOND_SAMPL_POINT_PERCENT = 70% also works.
        // (However the samplepoint defined in can_bitrate_data is critical. See comment above)
        //  1.0 M baud --> offsetSSP = 80 mtq
        //  2.0 M baud --> offsetSSP = 40 mtq
        //  2.5 M baud --> offsetSSP = 32 mtq
        //  4.0 M baud --> offsetSSP = 20 mtq
        //  5.0 M baud --> offsetSSP = 16 mtq
        //  8.0 M baud --> offsetSSP = 10 mtq
        // 10.0 M baud --> offsetSSP =  8 mtq
        uint32_t offsetSSP = data_bit_len * SECOND_SAMPL_POINT_PERCENT / 100;

        // The SSP offset + measured transceiver delay (see calculation in can_process()) must not exceed 127 mtq.
        // If offsetSSP > 64 (== baudrate < 2 Mbaud) --> turn off compensation
        if (offsetSSP > 0 && offsetSSP < 64 && inst->bitrate_data.Brp <= 2)
        {
            inst->tdc_offset = offsetSSP;

            // set TDCO and TDCF in register TDCR
            if (HAL_FDCAN_ConfigTxDelayCompensation(&inst->handle, inst->tdc_offset, 0) != HAL_OK) goto fail;
            // set TDC bit in register DBTP
            if (HAL_FDCAN_EnableTxDelayCompensation(&inst->handle) != HAL_OK) goto fail; // error detail in inst->handle.ErrorCode
        }
    }

    // -------------------- filters --------------------------

    // Store all user filters in can_filters into the processor's memory
    if (!can_apply_filters(inst))
        goto fail;

    // the user can define up to 8 filters
    bool has_filters = (inst->std_filter_count + inst->ext_filter_count) > 0;

    // If no user filters are defined --> accept all packets in FIFO 0 where they are sent over USB to the host.
    // Otherwise all packets that do not pass the user filters go to FIFO 1 where they only flash the blue LED.
    uint32_t non_matching = has_filters ? FDCAN_ACCEPT_IN_RX_FIFO1 : FDCAN_ACCEPT_IN_RX_FIFO0;

    if (HAL_FDCAN_ConfigGlobalFilter(&inst->handle, non_matching, non_matching, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK) goto fail;

    // ----------------------- start ---------------------------

    // sets inst->handle.State == HAL_FDCAN_STATE_BUSY
    uint32_t irq_mask = system_irq_save();
    inst->is_open = true;
    HAL_StatusTypeDef started = HAL_FDCAN_Start(&inst->handle);
    if (started != HAL_OK) inst->is_open = false;
    system_irq_restore(irq_mask);
    if (started != HAL_OK) goto fail;

    led_turn_TX(channel, false); // green off
    inst->is_open = true;
    return FBK_Success;
fail:
    inst->is_open = false;
    HAL_FDCAN_DeactivateNotification(&inst->handle, 0x3FFFFFFFU);
    can_stop_hardware(channel);
    HAL_FDCAN_DeInit(&inst->handle);
    return FBK_ErrorFromHAL;
}

void can_close_all()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        can_close(C);
    }
}

// Main-context shutdown must work even when a failed HAL call left State=ERROR.
// Follow the same INIT/CSR/CCE sequence as HAL_Stop without relying on State.
// A peripheral that cannot be quiesced requires a chip reset before returning.
static void can_stop_hardware(int channel)
{
    FDCAN_HandleTypeDef *handle = &can_inst[channel].handle;
    if (HAL_FDCAN_Stop(handle) == HAL_OK) return;
    SET_BIT(handle->Instance->CCCR, FDCAN_CCCR_INIT);
    uint32_t start = HAL_GetTick();
    while (!(handle->Instance->CCCR & FDCAN_CCCR_INIT))
        if (HAL_GetTick() - start >= 10U) system_fault_reset(SYSTEM_FAULT_CAN_STOP);
    CLEAR_BIT(handle->Instance->CCCR, FDCAN_CCCR_CSR);
    start = HAL_GetTick();
    while (handle->Instance->CCCR & FDCAN_CCCR_CSA)
        if (HAL_GetTick() - start >= 10U) system_fault_reset(SYSTEM_FAULT_CAN_STOP);
    SET_BIT(handle->Instance->CCCR, FDCAN_CCCR_CCE);
    handle->LatestTxFifoQRequest = 0;
    handle->State = HAL_FDCAN_STATE_READY;
}

// Disable the CAN peripheral and go off-bus
void can_close(int channel)
{
    can_class *inst = &can_inst[channel];
    if (inst->is_open) can_cancel_hardware(channel);
    inst->is_open = false;
    inst->suspended = false;
    if (inst->handle.State != HAL_FDCAN_STATE_RESET)
    {
        HAL_FDCAN_DeactivateNotification(&inst->handle, 0x3FFFFFFFU);
        can_stop_hardware(channel);
        if (HAL_FDCAN_DeInit(&inst->handle) != HAL_OK)
            error_assert(channel, APP_CanTxFail, true);
    }
#ifdef Candlelight
    USBD_ChannelBoundary(channel);
    buf_tx_cancel_all(channel);
    buf_clear_host_buffer(channel);
#endif
    can_reset(channel);
    error_init(channel);
    led_turn_TX(channel, true);
}

void can_suspend_all(void)
{
    for (int C = 0; C < CHANNEL_COUNT; ++C)
    {
        can_class *inst = &can_inst[C];
        if (!inst->is_open || inst->suspended) continue;
        can_cancel_hardware(C);
        can_stop_hardware(C);
        inst->is_open = false;
        inst->suspended = true;
#ifdef Candlelight
        buf_tx_cancel_all(C);
#endif
        inst->tx_pending = 0;
    }
}

void can_resume_all(void)
{
    for (int C = 0; C < CHANNEL_COUNT; ++C)
    {
        can_class *inst = &can_inst[C];
        if (!inst->suspended) continue;
        uint32_t mask = system_irq_save();
        inst->bit_count_total = 0;
        inst->busload_start_tick = HAL_GetTick();
        inst->is_open = true;
        HAL_StatusTypeDef result = HAL_FDCAN_Start(&inst->handle);
        if (result != HAL_OK) inst->is_open = false;
        else inst->suspended = false;
        system_irq_restore(mask);
        if (result != HAL_OK)
        {
            inst->suspended = false;
            inst->recovery_needed = true;
            error_assert(C, APP_CanTxFail, true);
        }
    }
}

// Cancel hardware requests, drain completions and retire each record once. Never
// rebuild or discard the software TX queue on a timeout or bus-off transition.
static bool can_cancel_hardware(int channel)
{
    can_class *inst = &can_inst[channel];
    uint32_t all = FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2;
    bool ok = HAL_FDCAN_AbortTxRequest(&inst->handle, all) == HAL_OK;
    uint32_t start = HAL_GetTick();
    while ((inst->handle.Instance->TXBRP & all) && HAL_GetTick() - start < 10U) {}
    uint32_t mask = system_irq_save();
    can_service_io(channel);
    bool pending = (inst->handle.Instance->TXBRP & all) != 0;
    system_irq_restore(mask);
    if (pending)
    {
        can_stop_hardware(channel);
#ifdef Candlelight
        buf_tx_cancel_all(channel);
#endif
        if (HAL_FDCAN_Start(&inst->handle) != HAL_OK) return false;
    }
    inst->tx_pending = 0;
    return ok;
}

// Called from Buffer. Stores a packet in the Tx FIFO
// Check HAL_FDCAN_GetTxFifoFreeLevel() and can_is_tx_allowed() before calling this function!
bool can_send_packet(int channel, FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data)
{
    can_class* inst = &can_inst[channel];


    HAL_StatusTypeDef status = HAL_FDCAN_AddMessageToTxFifoQ(&inst->handle, tx_header, tx_data);
    if (status != HAL_OK) // may be busy (state = HAL_FDCAN_STATE_BUSY)
    {
        // On error the HAL sets inst->handle.ErrorCode to HAL_FDCAN_ERROR_FIFO_FULL or HAL_FDCAN_ERROR_NOT_STARTED
        // Both errors can never happen, because this function is only called when CAN has been initialized and the FIFO is not full.
        error_assert(channel, APP_CanTxFail, true);
        return false;
    }

    if (inst->handle.Init.AutoRetransmission == ENABLE)
    {
        inst->last_tx_tick = HAL_GetTick();
        inst->tx_pending ++;
    }

    // Do not flash the Tx LED here! This was wrong in the legacy Candlelight firmware.
    // The packet has not been sent yet. It is still in the Tx FIFO and will stay there until an ACK is received.
    // When an ACK is received HAL_FDCAN_GetTxEvent() will return the Tx Event and the Tx LED will be flashed.
    return true;
}

static void can_service_io(int channel)
{
    can_class *inst = &can_inst[channel];
    if (!inst->is_open || inst->handle.State != HAL_FDCAN_STATE_BUSY) return;
    FDCAN_TxEventFifoTypeDef event;
    for (unsigned n = 0; n < 3 && (inst->handle.Instance->TXEFS & FDCAN_TXEFS_EFFL); ++n)
    {
        if (HAL_FDCAN_GetTxEvent(&inst->handle, &event) != HAL_OK) break;
        event.TxTimestamp = system_extend_timestamp((uint16_t)event.TxTimestamp);
#ifdef Candlelight
        buf_store_tx_echo(channel, &event);
#else
        if (GLB_UserFlags[channel] & USR_ReportTX) buf_store_tx_echo(channel, &event);
#endif
        if (inst->handle.Init.Mode == FDCAN_MODE_NORMAL)
        {
            FDCAN_RxHeaderTypeDef header = {0};
            header.Identifier = event.Identifier;
            header.IdType = event.IdType;
            header.RxFrameType = event.TxFrameType;
            header.DataLength = event.DataLength;
            header.FDFormat = event.FDFormat;
            header.BitRateSwitch = event.BitRateSwitch;
            inst->bit_count_total += can_calc_bit_count_in_frame(inst, &header);
        }
        if (inst->tx_pending > 0) --inst->tx_pending;
        led_flash_TX(channel);
    }
    uint32_t flags = inst->handle.Instance->IR;
    if (flags & FDCAN_FLAG_TX_EVT_FIFO_ELT_LOST) error_assert(channel, APP_CanTxFail, true);
    if (flags & (FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST | FDCAN_FLAG_RX_FIFO1_MESSAGE_LOST))
        error_assert(channel, APP_CanRxFail, true);
    inst->handle.Instance->IR = flags & (FDCAN_FLAG_TX_EVT_FIFO_ELT_LOST |
        FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST | FDCAN_FLAG_RX_FIFO1_MESSAGE_LOST);
    for (unsigned fifo = 0; fifo < 2; ++fifo)
    {
        uint32_t location = fifo ? FDCAN_RX_FIFO1 : FDCAN_RX_FIFO0;
        for (unsigned n = 0; n < 3 && HAL_FDCAN_GetRxFifoFillLevel(&inst->handle, location); ++n)
        {
            uint8_t data[64] = {0};
            FDCAN_RxHeaderTypeDef header;
            if (HAL_FDCAN_GetRxMessage(&inst->handle, location, &header, data) != HAL_OK) break;
            header.RxTimestamp = system_extend_timestamp((uint16_t)header.RxTimestamp);
            if (!fifo) buf_store_rx_packet(channel, &header, data);
            inst->bit_count_total += can_calc_bit_count_in_frame(inst, &header);
            led_flash_RX(channel);
        }
    }
#ifdef Candlelight
    // A transmission/cancellation may finish while events are being drained.
    // If a new event exists, leave reconciliation to the next IRQ to preserve its timestamp.
    uint32_t pending = inst->handle.Instance->TXBRP;
    uint32_t occurred = inst->handle.Instance->TXBTO;
    uint32_t canceled = inst->handle.Instance->TXBCF;
    if (!(inst->handle.Instance->TXEFS & FDCAN_TXEFS_EFFL))
        buf_tx_reconcile(channel, pending, occurred, canceled);
    inst->tx_pending = buf_tx_pending_count(channel);
#endif
}

static void can_sample_status(int channel)
{
    can_class *inst = &can_inst[channel];
    uint32_t mask = system_irq_save();
    FDCAN_ErrorCountersTypeDef counters;
    HAL_FDCAN_GetProtocolStatus(&inst->handle, &inst->cur_status);
#ifdef Candlelight
    HAL_FDCAN_GetErrorCounters(&inst->handle, &counters);
    error_update(channel, &inst->cur_status, &counters);
#else
    (void)counters;
#endif
    system_irq_restore(mask);
}

void can_interrupt(int channel)
{
    can_class *inst = &can_inst[channel];
    FDCAN_HandleTypeDef *handle = &inst->handle;
    uint32_t handled = handle->Instance->IR & handle->Instance->IE;
    if (!inst->is_open || handle->State != HAL_FDCAN_STATE_BUSY)
    {
        // No serviceable session can consume loss flags. Acknowledge every
        // enabled source so teardown/error states cannot starve the main loop.
        handle->Instance->IR = handled;
        return;
    }
    // Keep loss flags until can_service_io has recorded them.
    handle->Instance->IR = handled & ~(FDCAN_FLAG_TX_EVT_FIFO_ELT_LOST |
        FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST | FDCAN_FLAG_RX_FIFO1_MESSAGE_LOST);
    if (handled & (FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_BUS_OFF |
        FDCAN_IT_ARB_PROTOCOL_ERROR | FDCAN_IT_DATA_PROTOCOL_ERROR)) can_sample_status(channel);
    can_service_io(channel);
}

void can_process(int channel, uint32_t tick_now)
{
    can_class *inst = &can_inst[channel];
    if (!inst->is_open) return;
    char dbg_msg_buf[100];
#ifndef Candlelight
    can_service_io(channel);
#endif
    can_sample_status(channel);

    // ----------------------------- Transmit Timeout -----------------------------

    // If a message hangs longer than a few milliseconds in the Tx FIFO this means that it was not acknowledged.
    // The processor continues to send the message !!ETERNALLY!! producing a bus load of 95%.
    // Tx requests must be canceled by firmware to free CAN bus from the congestion.
    // the processor will never stop alone sending the same packet over and over again.
#ifdef Candlelight
    bool timed_out = inst->handle.Init.AutoRetransmission == ENABLE && buf_tx_timeout_due(channel, tick_now);
#else
    bool timed_out = inst->tx_pending > 0 && (int32_t)(tick_now - inst->last_tx_tick) >= CAN_TX_TIMEOUT;
#endif
    if (timed_out)
    {
        if (!can_cancel_hardware(channel)) inst->recovery_needed = true;
        error_assert(channel, APP_CanTxTimeout, true);
    }

    // ------------------------ print bitrate ---------------------------------

    // Important: This function must be called from the main loop, not from can_open()
    // otherwise the debug message is sent to the host before the response to command Open ("O") has been set.
    if (!inst->bitrate_printed_once && inst->is_open)
    {
        inst->bitrate_printed_once = true;
        can_print_info(channel);
    }

    // ------------------- print transceiver delay ----------------------------

    // The measurement of the transceiver delay is available after the first CAN FD message with BRS has been sent.
    // HAL_FDCAN_EnableTxDelayCompensation() must have been called during intialization to enable TDC.
    // The processor measures the delay through the transceiver chip, adds the TDC offset and stores it in TDCvalue.
    // It measures the delay from the dominant (falling) edge of the FDF bit between it's CAN Tx pin to the CAN Rx pin.
    // The unit of TDCvalue is mtq (minimum time quantums = one period of 160 MHz)
    // See page 26 in "ST - AN5348 - FDCAN Peripheral.pdf" in subfolder "Documentation".
    // If TDCvalue == 127 it is at the upper limit and must be ignored to avoid wrong results (this happens with 1M baud).
    // For the transceiver chip ADM3050E in the isolated CANable from MKS Makerbase the measured delay is 21 mtq = 131 ns.
    // The datasheet says maximum propagation delay TXD to RXD is 150 ns.
    // The ADM3050E supports up to 12 Mbit and works well even with 10 Mbit.
    if (!inst->delay_printed_once && inst->tdc_offset > 0 && inst->cur_status.TDCvalue > inst->tdc_offset && inst->cur_status.TDCvalue < 127)
    {
        inst->delay_printed_once  = true;
        uint32_t clock_MHz  = system_get_can_clock() / 1000000; // 160
        uint32_t chip_delay = inst->cur_status.TDCvalue - inst->tdc_offset;

        // chip_delay = 21 mtq --> 21 * 1000 / 160 = 131 ns
        sprintf(dbg_msg_buf, "Measured transceiver chip delay: %lu ns", chip_delay * 1000 / clock_MHz);
        control_send_debug_mesg(channel, dbg_msg_buf);
    }
}

// ATTENTION:
// The state BusOff (after 248 Tx errors) is a fatal situation where the CAN module is completely blocked.
// No further transmit operations are possible.
// And the FDCAN module will never recover alone from this situation.
// If the adapter is once in Bus Off state it will hang there eternally.
// The recovery process may take up to 200 ms for low baudrates (10 kBaud).
// The adapter easily goes into bus off state if you try to communicate between 2 adapters with a different baudrate.
// This function must be called after reporting BusOff to the host.
void can_recover_bus_off(int channel)
{
    can_class *inst = &can_inst[channel];
    if (inst->suspended) return;
    if (!inst->is_open && !inst->recovery_needed) return;
    bool off = inst->cur_status.BusOff != 0;
    if (!off && !inst->recovery_needed)
    {
        inst->recover_bus_off = false;
        return;
    }
#ifdef Candlelight
    // Keep a bus-off transition pending until it has actually entered the host queue.
    if ((GLB_UserFlags[channel] & USR_ErrorReport) && error_busoff_pending(channel)) return;
#endif
    uint32_t now = HAL_GetTick();
    if (now - inst->recovery_last_tick < 100U) return;
    if (inst->recover_bus_off && !inst->recovery_needed) return; // hardware is integrating the bus.
    inst->recovery_last_tick = now;
    if (inst->is_open) can_cancel_hardware(channel);
    can_stop_hardware(channel);
    uint32_t mask = system_irq_save();
    inst->is_open = true;
    HAL_StatusTypeDef started = HAL_FDCAN_Start(&inst->handle);
    if (started != HAL_OK) inst->is_open = false;
    system_irq_restore(mask);
    if (started != HAL_OK)
    {
        // An ERROR handle cannot be repaired by retrying Start alone.
        uint32_t mode = inst->handle.Init.Mode;
        inst->is_open = false;
        HAL_FDCAN_DeactivateNotification(&inst->handle, 0x3FFFFFFFU);
        HAL_FDCAN_DeInit(&inst->handle);
        inst->reinitializing = true;
        eFeedback reopened = can_open(channel, mode);
        inst->reinitializing = false;
        if (reopened != FBK_Success)
        {
            inst->recovery_needed = true;
            error_assert(channel, APP_CanTxFail, true);
            return;
        }
    }
#ifdef Candlelight
    buf_tx_cancel_all(channel);
#endif
    inst->tx_pending = 0;
    inst->recover_bus_off = off;
    inst->recovery_needed = false;
}

// Called every 100 ms from the main loop
// Calculate bus load, written (and hopefully tested well) by Nakanishi Kiyomaro.
void can_timer_100ms()
{
    uint32_t now = HAL_GetTick();
    for (int C = 0; C < CHANNEL_COUNT; ++C)
    {
        can_class *inst = &can_inst[C];
        if (!inst->is_open || !inst->busload_interval) continue;
        uint32_t elapsed = now - inst->busload_start_tick;
        if (elapsed < inst->busload_interval * 100U) continue;
        // CAN IRQs add bits. Claim the interval before computing/reporting it.
        uint32_t mask = system_irq_save();
        uint32_t bits = inst->bit_count_total;
        uint32_t bit_ns = inst->nom_bit_len_ns;
        inst->bit_count_total = 0;
        inst->busload_start_tick = now;
        system_irq_restore(mask);
        uint64_t weighted_ns = (uint64_t)bits * bit_ns * 1125U;
        uint64_t percent = weighted_ns / ((uint64_t)elapsed * 10000000U);
        uint8_t load = percent > 99U ? 99U : (uint8_t)percent;
        if (load || inst->old_busload_pct) control_report_busload(C, load);
        inst->old_busload_pct = load;
    }
}

// ----------------------------------------------------------------------------------------------

// ATTENTION: Deprecated! Read the manual.
// Set the nominal bitrate of the CAN peripheral
// Always samplepoint 75%. (In previous versions 87.5% was used which may produce Rx/Tx errors)
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
// IMPORTANT: Read the chapter "Samplepoint & Baudrate" in the HTML manual.
eFeedback can_set_baudrate(int channel, can_nom_bitrate bitrate)
{
    can_class* inst = &can_inst[channel];
    if (can_is_open(channel))
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus

    inst->bitrate_nominal.Seg1 = 239;
    inst->bitrate_nominal.Seg2 =  80;

    switch (bitrate)
    {
        case CAN_BITRATE_10K:
            inst->bitrate_nominal.Brp  = 50;
            break;
        case CAN_BITRATE_20K:
            inst->bitrate_nominal.Brp  = 25;
            break;
        case CAN_BITRATE_50K:
            inst->bitrate_nominal.Brp  = 10;
            break;
        case CAN_BITRATE_83K:
            inst->bitrate_nominal.Brp  = 6;
            break;
        case CAN_BITRATE_100K:
            inst->bitrate_nominal.Brp  = 5;
            break;
        case CAN_BITRATE_125K:
            inst->bitrate_nominal.Brp  = 4;   // 160 MHz / 4 / (1 + 239 + 80) = 125 kBaud
            break;                            // (1 + 239)   / (1 + 239 + 80) = 75%
        case CAN_BITRATE_250K:
            inst->bitrate_nominal.Brp  = 2;
            break;
        case CAN_BITRATE_500K:
            inst->bitrate_nominal.Brp  = 1;
            break;
        case CAN_BITRATE_800K:
            inst->bitrate_nominal.Brp  = 1;
            inst->bitrate_nominal.Seg1 = 149;
            inst->bitrate_nominal.Seg2 = 50;
            break;
        case CAN_BITRATE_1000K:
            inst->bitrate_nominal.Brp  = 1;
            inst->bitrate_nominal.Seg1 = 119;
            inst->bitrate_nominal.Seg2 = 40;
            break;
        default:
            return FBK_InvalidParameter;
    }

    bitlimits* limits = utils_get_bit_limits();
    inst->bitrate_nominal.Sjw = MIN(inst->bitrate_nominal.Seg2, limits->nom_sjw_max);

    // Check if the settings are supported by the processor.
    // If not the user must call can_set_nom_bit_timing() instead.
    if (!IS_FDCAN_NOMINAL_PRESCALER(inst->bitrate_nominal.Brp)  ||
        !IS_FDCAN_NOMINAL_TSEG1    (inst->bitrate_nominal.Seg1) ||
        !IS_FDCAN_NOMINAL_TSEG2    (inst->bitrate_nominal.Seg2))
    {
        inst->bitrate_nominal.Brp = 0; // baudrate not valid
        return FBK_ParamOutOfRange;
    }
    return FBK_Success;
}

// ATTENTION: Deprecated! Read the manual.
// Set the data bitrate of the CAN peripheral
// Samplepoint = 75%, except for 8 MBaud it must be 50% because 75% does not work.
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
// IMPORTANT: Read the chapter "Samplepoint & Baudrate" in the HTML manual.
eFeedback can_set_data_baudrate(int channel, can_data_bitrate bitrate)
{
    can_class* inst = &can_inst[channel];
    if (can_is_open(channel))
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus

#ifdef CAN_MAX_DATA_BITRATE
    if (bitrate == CAN_DATA_BITRATE_8M) return FBK_ParamOutOfRange;
#endif
    inst->bitrate_data.Seg1 = 29;
    inst->bitrate_data.Seg2 = 10;

    switch (bitrate)
    {
        case CAN_DATA_BITRATE_500K:
            inst->bitrate_data.Brp  = 8;
            break;
        case CAN_DATA_BITRATE_1M:
            inst->bitrate_data.Brp  = 4;
            break;
        case CAN_DATA_BITRATE_2M:
            inst->bitrate_data.Brp  = 2;
            break;
        case CAN_DATA_BITRATE_4M:
            inst->bitrate_data.Brp  = 2;  // 160 MHz / 2 / (1 + 14 + 5) = 4 MBaud
            inst->bitrate_data.Seg1 = 14; // (1 + 14)    / (1 + 14 + 5) = 75%
            inst->bitrate_data.Seg2 = 5;
            break;
        case CAN_DATA_BITRATE_5M:
            inst->bitrate_data.Brp  = 2;
            inst->bitrate_data.Seg1 = 11;
            inst->bitrate_data.Seg2 = 4;
            break;
        // For any strange reason the STM32G431 works at 8 Mbaud only if the samplepoint is 50%.
        // But at 10 Mbaud it works with 75%. Very weird!
        case CAN_DATA_BITRATE_8M:
            inst->bitrate_data.Brp  = 2; // 160 MHz / 2 / (1 + 4 + 5) = 8 MBaud
            inst->bitrate_data.Seg1 = 4; // (1 + 4)     / (1 + 4 + 5) = 50%
            inst->bitrate_data.Seg2 = 5;
            break;
        default:
            return FBK_InvalidParameter;
    }

    bitlimits* limits = utils_get_bit_limits();
    inst->bitrate_data.Sjw = MIN(inst->bitrate_data.Seg2, limits->fd_sjw_max);

    // Check if the settings are supported by the processor.
    // If not the user must call can_set_data_bit_timing() instead.
    if (!IS_FDCAN_DATA_PRESCALER(inst->bitrate_data.Brp)  ||
        !IS_FDCAN_DATA_TSEG1    (inst->bitrate_data.Seg1) ||
        !IS_FDCAN_DATA_TSEG2    (inst->bitrate_data.Seg2))
    {
        inst->bitrate_data.Brp = 0; // baudrate not valid
        return FBK_ParamOutOfRange;
    }
    return FBK_Success;
}

// Set the nominal bitrate configuration of the CAN peripheral
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
eFeedback can_set_nom_bit_timing(int channel, uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw)
{
    can_class* inst = &can_inst[channel];
    if (can_is_open(channel))
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus

    if (!IS_FDCAN_NOMINAL_PRESCALER(BRP)  ||
        !IS_FDCAN_NOMINAL_TSEG1    (Seg1) ||
        !IS_FDCAN_NOMINAL_TSEG2    (Seg2) ||
        !IS_FDCAN_NOMINAL_SJW      (Sjw) || Sjw > Seg1 || Sjw > Seg2)
            return FBK_ParamOutOfRange;

    if ((uint64_t)CAN_MIN_NOMINAL_BITRATE * BRP * (1U + Seg1 + Seg2) > system_get_can_clock())
        return FBK_ParamOutOfRange;

    inst->bitrate_nominal.Brp  = BRP;
    inst->bitrate_nominal.Seg1 = Seg1;
    inst->bitrate_nominal.Seg2 = Seg2;
    inst->bitrate_nominal.Sjw  = Sjw;
    return FBK_Success;
}

// Set the data bitrate configuration of the CAN peripheral
// FD is selected separately by the host START flag; retain this timing while closed.
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
eFeedback can_set_data_bit_timing(int channel, uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw)
{
    can_class* inst = &can_inst[channel];
    if (can_is_open(channel))
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus

    if (!IS_FDCAN_DATA_PRESCALER(BRP)  ||
        !IS_FDCAN_DATA_TSEG1    (Seg1) ||
        !IS_FDCAN_DATA_TSEG2    (Seg2) ||
        !IS_FDCAN_DATA_SJW      (Sjw) || Sjw > Seg1 || Sjw > Seg2)
            return FBK_ParamOutOfRange;

#ifdef CAN_MAX_DATA_BITRATE
    // Compare the rational bitrate without rounding a value above the ceiling down.
    if ((uint64_t)CAN_MAX_DATA_BITRATE * BRP * (1U + Seg1 + Seg2) < system_get_can_clock())
        return FBK_ParamOutOfRange;
#endif
    inst->bitrate_data.Brp  = BRP;
    inst->bitrate_data.Seg1 = Seg1;
    inst->bitrate_data.Seg2 = Seg2;
    inst->bitrate_data.Sjw  = Sjw;
    return FBK_Success;
}

// Prints a string containing the baudrate and samplepoint to the debug output.
// Although this is very simple many application and firmware developers do this calculation wrong. (see sourcecode of Cangaroo)
// It is very important to verify that baudrate and samplepoint have been set correctly by the user.
// Therefore this firmware prints them as a debug message so the user can verify the settings.
// Additionally this function can print the TDC configuration and if the pin BOOT0 is enabled.
// This function is only called if CAN is opened.
void can_print_info(int channel)
{
    if ((GLB_UserFlags[channel] & USR_DebugReport) == 0)
        return;

    can_class* inst = &can_inst[channel];

    char buf[200];
    if (system_reset_reason())
    {
        sprintf(buf, "Previous reset reason: %lu", system_reset_reason());
        control_send_debug_mesg(channel, buf);
    }
    if (inst->handle.Init.Mode != FDCAN_MODE_NORMAL)
    {
        char* mode = "Invalid";
        switch (inst->handle.Init.Mode)
        {
            case FDCAN_MODE_RESTRICTED_OPERATION: mode = "Restricted";        break;
            case FDCAN_MODE_BUS_MONITORING:       mode = "Monitoring";        break;
            case FDCAN_MODE_INTERNAL_LOOPBACK:    mode = "Internal Loopback"; break;
            case FDCAN_MODE_EXTERNAL_LOOPBACK:    mode = "External Loopback"; break;
        }
        sprintf(buf, "Operation Mode: %s", mode);
        control_send_debug_mesg(channel, buf);
    }

    // print "Nominal: 500k baud, 87.5%; Data: 2M baud, 75.0%; Perfect match: Yes"
    utils_format_bitrate(buf,               "Nominal", &inst->bitrate_nominal);
    utils_format_bitrate(buf + strlen(buf), "; Data",  &inst->bitrate_data);

    if (can_using_FD(channel))
    {
        // See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
        strcat(buf, "; Perfect match: ");
        strcat(buf, inst->bitrate_nominal.Brp == inst->bitrate_data.Brp ? "Yes" : "No");
    }
    control_send_debug_mesg(channel, buf);

    // optional additional information: pin BOOT0 and TDC offset
    if (false)
    {
        // Print Transceiver Delay Compensation
        if (inst->tdc_offset > 0)
            sprintf(buf, "TDC offset: %lu mtq", inst->tdc_offset);
        else
            strcpy(buf, "TDC: not used");

        // Print enable status of pin BOOT0
        strcat(buf, ", Pin BOOT0: ");
        strcat(buf, (system_is_option_enabled(OPT_BOOT0_Enable)) ? "enabled" : "disabled");

        control_send_debug_mesg(channel, buf);
    }
}

// ----------------------------------------------------------------------------------------------

// The processor allows up to 28 standard filters and up to 8 extended filters.
// Nobody needs so many filters -> allow 8 user filters.
// Rx FIFO 0 receives all packets that pass. They are sent to the host application over USB.
// Rx FIFO 1 receives all packets that are rejected, they only flash the blue LED.
// Each FIFO can store 3 Rx packets before it is full.
// ---------------------------------------------------------
// While all industry CAN bus adapters allow to set filters after opening the adapter, the STM32 processor is very restricted.
// The values inst->handle.Init.StdFiltersNbr and ExtFiltersNbr cannot be modified anymore after opening the adapter.
// But HAL_FDCAN_ConfigFilter() can be called after opening the adapter.
// So the only possible filter modification after opening the adapter is to modify ONE existing filter.
// The filter type must be the same (11 bit or 29 bit).
eFeedback can_set_mask_filter(int channel, bool extended, uint32_t filter, uint32_t mask)
{
    can_class* inst = &can_inst[channel];

    if (can_is_open(channel) && !inst->is_open) return FBK_AdapterMustBeClosed;
    int tot_filters = inst->std_filter_count + inst->ext_filter_count;
    if (tot_filters >= MAX_FILTERS)
        return FBK_InvalidParameter;

    uint32_t maximum = extended ? 0x1FFFFFFF : 0x7FF;
    if (filter > maximum || mask > maximum)
        return FBK_ParamOutOfRange;

    if (inst->is_open)
    {
        // only one existing filter can be modified if the adapter is already open
        if (tot_filters != 1)
            return FBK_AdapterMustBeClosed;

        // the filter to be modified must be from the same type
        if (extended != (inst->ext_filter_count == 1))
            return FBK_AdapterMustBeClosed;

        // modify the one and only filter at index 0
        inst->ext_filter_count = 0;
        inst->std_filter_count = 0;
        tot_filters = 0;
    }

    FDCAN_FilterTypeDef* last_filter = &inst->filters[tot_filters];
    last_filter->IdType       = extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    last_filter->FilterIndex  = extended ? inst->ext_filter_count : inst->std_filter_count;
    last_filter->FilterType   = FDCAN_FILTER_MASK;
    last_filter->FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    last_filter->FilterID1    = filter;
    last_filter->FilterID2    = mask;

    if (extended) inst->ext_filter_count ++;
    else          inst->std_filter_count ++;

    if (inst->is_open && !can_apply_filters(inst))
        return FBK_ErrorFromHAL;

    return FBK_Success;
}

// Store all user filters in can_filters into the processor's memory
bool can_apply_filters(can_class* inst)
{
    // the user can define up to 8 filters
    int tot_filters = inst->std_filter_count + inst->ext_filter_count;
    for (int i=0; i<tot_filters; i++)
    {
        if (HAL_FDCAN_ConfigFilter(&inst->handle, &inst->filters[i]) != HAL_OK)
            return false; // error detail in inst->handle.ErrorCode
    }
    return true;
}

// clear all filters
eFeedback can_clear_filters(int channel)
{
    can_class* inst = &can_inst[channel];

    if (can_is_open(channel))
        return FBK_AdapterMustBeClosed; // cannot clear filters while on bus

    inst->ext_filter_count = 0;
    inst->std_filter_count = 0;
    return FBK_Success;
}

// ----------------------------------------------------------------------------------------------

// Some boards have a 120 Ohm termination resistor that can be enabled by a GPIO pin.
// returns false if the board does not support this feature
bool can_set_termination(int channel, bool enable)
{
    if (SET_TermPins[channel] < 0)
        return false; // --> return FBK_UnsupportedFeature to host

    HAL_GPIO_WritePin(SET_TermPorts[channel], SET_TermPins[channel], enable ? TERMINATOR_ON : TERMINATOR_OFF);
    can_inst[channel].termination_on = enable;
    return true;
}

bool can_get_termination(int channel, bool* enabled)
{
    if (SET_TermPins[channel] < 0)
        return false; // --> return FBK_UnsupportedFeature to host

    *enabled = can_inst[channel].termination_on;
    return true;
}

// ----------------------------------------------------------------------------------------------

// interval =   0 --> disable busload report
// interval =   1 --> report busload every 100 ms     (minimum)
// interval =   7 --> report busload every 700 ms
// interval = 100 --> report busload every 10 seconds (maximum)
eFeedback can_enable_busload(int channel, uint32_t interval)
{
    if (interval > 100)
        return FBK_ParamOutOfRange;

    uint32_t mask = system_irq_save();
    can_inst[channel].busload_interval = interval;
    can_inst[channel].bit_count_total = 0;
    can_inst[channel].busload_counter = 0;
    can_inst[channel].old_busload_pct = 0;
    can_inst[channel].busload_start_tick = HAL_GetTick();
    system_irq_restore(mask);
    return FBK_Success;
}

// ----------------------------------------------------------------------------------------------

// check if any channel is open
bool can_is_any_open()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        if (can_is_open(C))
            return true;
    }
    return false;
}

// Return bus status
bool can_is_open(int channel)
{
    can_class *inst = &can_inst[channel];
    return inst->is_open || inst->suspended || inst->recovery_needed;
}

// LED readiness must reflect actual hardware ownership, not the logical-open
// state retained during suspend or recovery. Use the already sampled status:
// reading PSR here would consume protocol errors before the error reporter.
bool can_is_ready(int channel)
{
    can_class *inst = &can_inst[channel];
    return inst->is_open && !inst->suspended && !inst->recovery_needed &&
        !inst->recover_bus_off && !inst->cur_status.BusOff &&
        inst->handle.State == HAL_FDCAN_STATE_BUSY;
}

// > 128 errors have occurred
bool can_is_passive(int channel)
{
    return can_inst[channel].cur_status.ErrorPassive;
}

// true if data baudrate has been set, otherwise CAN classic
bool can_using_FD(int channel)
{
#ifdef Slcan
    return can_inst[channel].bitrate_data.Brp > 0;
#else
    return can_inst[channel].fd_enabled;
#endif
}

eFeedback can_select_fd_mode(int channel, bool enabled)
{
    can_class *inst = &can_inst[channel];
    if (can_is_open(channel)) return FBK_AdapterMustBeClosed;
    if (enabled && (!inst->bitrate_data.Brp || !inst->bitrate_nominal.Brp)) return FBK_BaudrateNotSet;
    if (enabled && can_calc_baud(&inst->bitrate_nominal) < CAN_MIN_NOMINAL_BITRATE_FD)
        return FBK_ParamOutOfRange;
    if (enabled && can_calc_baud(&inst->bitrate_data) < can_calc_baud(&inst->bitrate_nominal))
        return FBK_ParamOutOfRange;
    inst->fd_enabled = enabled;
    return FBK_Success;
}

// true if data bitrate greater than nominal bitrate
bool can_using_BRS(int channel)
{
    can_class* inst = &can_inst[channel];
    return can_calc_baud(&inst->bitrate_data) > can_calc_baud(&inst->bitrate_nominal);
}

eFeedback can_is_tx_allowed(int channel)
{
    can_class* inst = &can_inst[channel];

    if (inst->suspended || inst->recovery_needed)
        return FBK_BusIsOff; // retain queued records until operation resumes.
    if (!inst->is_open)
        return FBK_AdapterMustBeOpen;
    if (inst->handle.Init.Mode == FDCAN_MODE_BUS_MONITORING)
        return FBK_NoTxInSilentMode;
    if (inst->cur_status.BusOff)
        return FBK_BusIsOff;
    return FBK_Success;
}

// Check if at least one Tx FIFO buffer is available
bool can_is_tx_fifo_free(int channel)
{
    return HAL_FDCAN_GetTxFifoFreeLevel(&can_inst[channel].handle) > 0;
}

// Return reference to CAN handle
FDCAN_HandleTypeDef *can_get_handle(int channel)
{
    return &can_inst[channel].handle;
}

// ---------------------------------------------------------------------------------------------------

// Calculate the bit count of a CAN frame. Data bits with BRS are converted to nominal baudrate.
// This code was written by Nakanishi Kiyomaro and was hopefully tested well.
uint32_t can_calc_bit_count_in_frame(can_class* inst, FDCAN_RxHeaderTypeDef* header)
{
    if (inst->busload_interval == 0)
        return 0;

    // Bit number for each frame type with zero data length
    const uint32_t CAN_BIT_NBR_WOD_CBFF        = 47;
    const uint32_t CAN_BIT_NBR_WOD_CEFF        = 67;
    const uint32_t CAN_BIT_NBR_WOD_FBFF_ARBIT  = 30;
    const uint32_t CAN_BIT_NBR_WOD_FEFF_ARBIT  = 49;
    const uint32_t CAN_BIT_NBR_WOD_FXFF_DATA_S = 26;
    const uint32_t CAN_BIT_NBR_WOD_FXFF_DATA_L = 30;

    uint32_t byte_count = header->FDFormat == FDCAN_FD_CAN ? utils_dlc_to_byte_count(header->DataLength) : MIN(header->DataLength, 8);

    uint32_t bit_count;
    if (header->RxFrameType == FDCAN_REMOTE_FRAME && header->IdType == FDCAN_STANDARD_ID)
    {
        bit_count = CAN_BIT_NBR_WOD_CBFF;
    }
    else if (header->RxFrameType == FDCAN_REMOTE_FRAME && header->IdType == FDCAN_EXTENDED_ID)
    {
        bit_count = CAN_BIT_NBR_WOD_CEFF;
    }
    else if (header->FDFormat == FDCAN_CLASSIC_CAN && header->IdType == FDCAN_STANDARD_ID)
    {
        bit_count = CAN_BIT_NBR_WOD_CBFF + byte_count * 8;
    }
    else if (header->FDFormat == FDCAN_CLASSIC_CAN && header->IdType == FDCAN_EXTENDED_ID)
    {
        bit_count = CAN_BIT_NBR_WOD_CEFF + byte_count * 8;
    }
    else // for FD frames
    {
        if (header->IdType == FDCAN_STANDARD_ID) bit_count = CAN_BIT_NBR_WOD_FBFF_ARBIT;
        else                                     bit_count = CAN_BIT_NBR_WOD_FEFF_ARBIT;

        // calculate all bits separately that are affected by the bitrate switching (data + CRC)
        uint32_t brs_bits = byte_count * 8;
        if (byte_count <= 16)
            brs_bits += CAN_BIT_NBR_WOD_FXFF_DATA_S; // Short CRC
        else
            brs_bits += CAN_BIT_NBR_WOD_FXFF_DATA_L; // Long CRC

        // Convert data bit time to nominal bit time
        if (header->BitRateSwitch == FDCAN_BRS_ON)
        {
            // As double arithmetic is not available here --> calculate with integers multiplied with 1 million
            uint64_t factor = 1 + inst->bitrate_data.Seg1 + inst->bitrate_data.Seg2; // Tq in one bit (data)
            factor *= inst->bitrate_data.Brp;
            factor *= 1000000;
            factor /= 1 + inst->bitrate_nominal.Seg1 + inst->bitrate_nominal.Seg2;   // Tq in one bit (nominal)
            factor /= inst->bitrate_nominal.Brp;

            bit_count += ((uint32_t)brs_bits * factor) / 1000000;
        }
        else // BRS = off
        {
            bit_count += brs_bits;
        }
    }
    return bit_count;
}

