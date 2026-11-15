/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Hubert Denkmair
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "buffer.h"
#include "error.h"
#include "control.h"
#include "system.h"
#include "utils.h"
#include "settings.h"
#include "usb_class.h"
#include "candlelight_def.h"
#include "can.h"

// ----- Globals
extern eUserFlags GLB_UserFlags[CHANNEL_COUNT];

// Global flag that enables the new ElmüSoft protocol for maximum USB throughput (Candlelight only).
// It is not possible to enable the ElmüSoft protocol for an individual channel, 
// because ElmüSoft uses different USB interfaces while Legacy routes all traffic through the first interface.
bool GLB_ProtoElmue = false;

// ----- Class Instance
buf_class  buf_inst[CHANNEL_COUNT] = {0};

// ----- Private Methods
void buf_process_host (int channel, buf_class* usb_buf);
void buf_process_can  (int channel, buf_class* usb_buf, buf_class* can_buf);
void buf_clear_buffers(int channel, bool clear_can, bool clear_host);

// public
void buf_init()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        buf_clear_buffers(C, true, true);
    }
}
// public
void buf_clear_can_buffer(int channel)
{
    buf_clear_buffers(channel, true, false);
}
// private
void buf_clear_buffers(int channel, bool clear_can, bool clear_host)
{
    buf_class* inst = &buf_inst[channel];
    uint32_t mask = system_irq_save();

    if (clear_can)
    {
        list_init(&inst->list_can_pool);
        list_init(&inst->list_to_can);

        // add the 64 entries to the can pool ringbuffers
        for (int i=0; i < CAN_QUEUE_SIZE; i++)
        {
            list_add_tail(&inst->can_pool_buffer[i].list, &inst->list_can_pool);
        }
    }
    if (clear_host)
    {
        list_init(&inst->list_host_pool);
        list_init(&inst->list_to_host);

        // add the 70 entries to the host pool ringbuffers
        for (int i=0; i < HOST_QUEUE_SIZE; i++)
        {
            list_add_tail(&inst->host_pool_buffer[i].list, &inst->list_host_pool);
        }
        inst->RxPending = false;
        inst->in_flight = NULL;
        memset(inst->pending_tx, 0, sizeof(inst->pending_tx));
        memset(inst->pending_hw, 0, sizeof(inst->pending_hw));
        inst->TxBusy = false;
        inst->SendZLP = false;
    }
    system_irq_restore(mask);
}

// ---------------------------------------------------------------------------------------------------

// called approx 100 times in one millisecond from the main loop
void buf_process(int channel, uint32_t tick_now)
{
    buf_class* can_buf = &buf_inst[channel];
    buf_class* usb_buf = buf_get_inst_for_usb(channel);
    
    buf_process_host(channel, usb_buf);
    buf_process_can (channel, usb_buf, can_buf);

    // The APP_xxx errors are deleted after sending them to the host.
    // Refresh current resource-exhaustion flags until capacity becomes available.
    if (list_is_empty(&can_buf->list_can_pool))  error_assert(channel, APP_CanTxOverflow, false);
    if (list_is_empty(&usb_buf->list_host_pool)) error_assert(channel, APP_UsbInOverflow, false);
}

// called from the main loop
// send a CAN packet to the host if list_to_host has data
void buf_process_host(int channel, buf_class* usb_buf)
{
    if (!USBD_TrafficReady() || usb_buf->TxBusy) return;
    // Keep the object until the last USB packet completes. Clear-halt can replay it.
    if (!usb_buf->in_flight)
        usb_buf->in_flight = buf_get_frame_locked(&usb_buf->list_to_host);
    if (usb_buf->in_flight)
        USBD_SendFrameToHost(channel, &usb_buf->in_flight->frame);
}

void buf_release_in_flight(int channel)
{
    buf_class *usb = &buf_inst[channel];
    uint32_t mask = system_irq_save();
    if (usb->in_flight)
    {
        list_add_tail(&usb->in_flight->list, &usb->list_host_pool);
        usb->in_flight = NULL;
    }
    usb->TxBusy = false;
    system_irq_restore(mask);
}

// Discard only this channel's old host records; legacy channels share endpoint 0x81.
void buf_clear_host_buffer(int channel)
{
    buf_class *usb = buf_get_inst_for_usb(channel);
    uint32_t mask = system_irq_save();
    list_item *item = usb->list_to_host.next;
    while (item != &usb->list_to_host)
    {
        list_item *next = item->next;
        kHostFrameObject *obj = list_entry(item, kHostFrameObject, list);
        if (GLB_ProtoElmue || obj->frame.channel == channel)
        {
            list_remove(item);
            list_add_tail(item, &usb->list_host_pool);
        }
        item = next;
    }
    if (usb->in_flight && (GLB_ProtoElmue || usb->in_flight->frame.channel == channel))
    {
        USBD_CancelIn(GLB_ProtoElmue ? channel : 0);
        buf_release_in_flight(GLB_ProtoElmue ? channel : 0);
    }
    system_irq_restore(mask);
}

void buf_tx_finish(int channel, uint8_t marker, uint32_t timestamp, bool success)
{
    buf_class *can = &buf_inst[channel];
    buf_class *usb = buf_get_inst_for_usb(channel);
    unsigned slot = marker & 3U;
    uint32_t mask = system_irq_save();
    if (slot >= 3 || !can->pending_tx[slot] || can->pending_marker[slot] != marker)
    {
        system_irq_restore(mask);
        return; // stale/duplicate event, including an event from a canceled request.
    }
    kHostFrameObject *obj = can->pending_tx[slot];
    can->pending_tx[slot] = NULL;
    can->pending_hw[slot] = 0;
    if (!success) error_assert(channel, APP_CanTxFail, true);
    if (GLB_ProtoElmue)
    {
        uint8_t user_marker = ((kTxFrameElmue *)&obj->frame)->marker;
        if (!success || !(GLB_UserFlags[channel] & USR_ReportTX))
        {
            list_add_tail(&obj->list, &usb->list_host_pool);
            system_irq_restore(mask);
            return;
        }
        kTxEchoElmue *echo = (void *)&obj->frame;
        memset(&obj->frame, 0, sizeof(obj->frame));
        echo->header.size = sizeof(*echo);
        echo->header.msg_type = MSG_TxEcho;
        echo->marker = user_marker;
        echo->timestamp = timestamp;
        if (!(GLB_UserFlags[channel] & USR_Timestamp)) echo->header.size -= 4;
    }
    else
    {
        // gs_usb has no failed-completion packet. On cancellation, release its
        // context with one echo plus an error report. Successful echoes use TX events.
        if (obj->frame.flags & FRM_FDF) obj->frame.pack_FD.timestamp_us = timestamp;
        else obj->frame.pack_classic.timestamp_us = timestamp;
    }
    list_add_tail(&obj->list, &usb->list_to_host);
    system_irq_restore(mask);
}

unsigned buf_tx_pending_count(int channel)
{
    unsigned count = 0;
    uint32_t mask = system_irq_save();
    for (unsigned i = 0; i < 3; ++i) count += buf_inst[channel].pending_tx[i] != NULL;
    system_irq_restore(mask);
    return count;
}

bool buf_tx_timeout_due(int channel, uint32_t now)
{
    bool due = false;
    uint32_t mask = system_irq_save();
    for (unsigned i = 0; i < 3; ++i)
        if (buf_inst[channel].pending_tx[i] &&
            (int32_t)(now - buf_inst[channel].pending_since[i]) >= 500) due = true;
    system_irq_restore(mask);
    return due;
}

void buf_tx_reconcile(int channel, uint32_t pending, uint32_t occurred, uint32_t canceled)
{
    // Call only after draining the event FIFO, with the CAN interrupt masked.
    buf_class *can = &buf_inst[channel];
    for (unsigned i = 0; i < 3; ++i)
        if (can->pending_tx[i] && can->pending_hw[i] && !(pending & can->pending_hw[i]) &&
            ((occurred | canceled) & can->pending_hw[i]))
            buf_tx_finish(channel, can->pending_marker[i], system_get_timestamp(),
                          (occurred & can->pending_hw[i]) != 0);
}

void buf_tx_cancel_all(int channel)
{
    // Hardware must be stopped or confirmed free before retiring these records.
    uint32_t mask = system_irq_save();
    for (unsigned i = 0; i < 3; ++i)
        if (buf_inst[channel].pending_tx[i])
            buf_tx_finish(channel, buf_inst[channel].pending_marker[i], system_get_timestamp(), false);
    system_irq_restore(mask);
}

void buf_process_can(int channel, buf_class* usb, buf_class* can)
{
    if (list_is_empty(&can->list_to_can)) return;
    eFeedback allowed = can_is_tx_allowed(channel);
    if (allowed == FBK_BusIsOff) return; // retain ownership until recovery, with backpressure.
    if (allowed == FBK_Success && !can_is_tx_fifo_free(channel)) return;
    uint32_t mask = system_irq_save();
    unsigned slot;
    for (slot = 0; slot < 3 && can->pending_tx[slot]; ++slot) {}
    if (slot == 3) { system_irq_restore(mask); return; }
    kHostFrameObject *completion = buf_get_frame_locked(&usb->list_host_pool);
    if (!completion) { system_irq_restore(mask); return; }
    kHostFrameObject *request = buf_get_frame_locked(&can->list_to_can);
    if (!request)
    {
        list_add_tail(&completion->list, &usb->list_host_pool);
        system_irq_restore(mask);
        return;
    }
    completion->frame = request->frame;
    uint8_t marker = (uint8_t)((can->tx_sequence++ << 2) | slot);
    can->pending_tx[slot] = completion;
    can->pending_marker[slot] = marker;
    can->pending_since[slot] = HAL_GetTick();
    can->pending_hw[slot] = 0;

    uint32_t id;
    uint8_t flags, dlc;
    uint8_t *data;
    if (GLB_ProtoElmue)
    {
        kTxFrameElmue *frame = (void *)&request->frame;
        id = frame->can_id; flags = frame->flags; data = frame->data_start;
        unsigned bytes = frame->header.size - sizeof(*frame);
        dlc = (id & CAN_ID_RTR) ? (bytes ? data[0] : 0) : utils_byte_count_to_dlc(bytes);
    }
    else
    {
        id = request->frame.can_id; flags = request->frame.flags;
        dlc = request->frame.can_dlc; data = request->frame.pack_FD.data;
    }
    FDCAN_TxHeaderTypeDef header = {0};
    header.Identifier = id & ((id & CAN_ID_29Bit) ? CAN_MASK_29 : CAN_MASK_11);
    header.IdType = (id & CAN_ID_29Bit) ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    header.TxFrameType = (id & CAN_ID_RTR) ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    header.DataLength = dlc;
    header.FDFormat = (flags & FRM_FDF) ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    header.BitRateSwitch = (flags & FRM_BRS) ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    header.ErrorStateIndicator = can_is_passive(channel) ? FDCAN_ESI_PASSIVE : FDCAN_ESI_ACTIVE;
    header.TxEventFifoControl = FDCAN_STORE_TX_EVENTS;
    header.MessageMarker = marker;
    bool sent = !request->rejected && allowed == FBK_Success && (!(flags & FRM_FDF) || can_using_FD(channel)) &&
                can_send_packet(channel, &header, data);
    if (sent)
        can->pending_hw[slot] = HAL_FDCAN_GetLatestTxFifoQRequestBuffer(can_get_handle(channel));
    else buf_tx_finish(channel, marker, system_get_timestamp(), false);
    list_add_tail(&request->list, &can->list_can_pool);
    system_irq_restore(mask);
}

// a RX packet has been received from CAN bus.
// frame_data is a 64 byte buffer with the received / sent data bytes
// append the frame to list_to_host
void buf_store_rx_packet(int channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *frame_data)
{
    buf_class* usb_buf = buf_get_inst_for_usb(channel);
    
    kHostFrameObject* obj_to_host = buf_get_frame_locked(&usb_buf->list_host_pool);
    if (!obj_to_host)
    {
        error_assert(channel, APP_UsbInOverflow, true);
        return;
    }

    uint32_t can_id;
    if (rx_header->IdType == FDCAN_EXTENDED_ID)
        can_id = (rx_header->Identifier & CAN_MASK_29) | CAN_ID_29Bit;
    else
        can_id = (rx_header->Identifier & CAN_MASK_11);

    if (rx_header->RxFrameType == FDCAN_REMOTE_FRAME)
        can_id |= CAN_ID_RTR;

    uint8_t flags = 0;
    if (rx_header->FDFormat == FDCAN_FD_CAN)
    {
        flags |= FRM_FDF;
        if (rx_header->BitRateSwitch       == FDCAN_BRS_ON)      flags |= FRM_BRS;
        if (rx_header->ErrorStateIndicator == FDCAN_ESI_PASSIVE) flags |= FRM_ESI;
    }

    uint8_t can_dlc = rx_header->DataLength;

    // ------------------------

    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        uint8_t byte_count;
        if (can_id & CAN_ID_RTR)
        {
            // For remote frames the DLC from the Rx packet is transmitted in the first data byte to the host.
            frame_data[0] = can_dlc;
            byte_count = 1;            
        }
        else byte_count = (rx_header->FDFormat == FDCAN_FD_CAN) ? utils_dlc_to_byte_count(can_dlc) : MIN(can_dlc, 8);

        kRxFrameElmue* frame = (kRxFrameElmue*)&obj_to_host->frame;
        frame->header.size     = sizeof(kRxFrameElmue) + byte_count;
        frame->header.msg_type = MSG_RxFrame;
        frame->flags           = flags;
        frame->can_id          = can_id;
        frame->timestamp       = rx_header->RxTimestamp; // 32 bit

        if (GLB_UserFlags[channel] & USR_Timestamp)
        {
            memcpy(frame->data_use_stamp, frame_data, byte_count);
        }
        else
        {
            frame->header.size -= 4;
            memcpy(frame->data_no_stamp, frame_data, byte_count);
        }
    }
    else // legacy Geschwister Schneider protocol
    {
        kHostFrameLegacy* frame = &obj_to_host->frame;
        frame->channel  = channel;
        frame->reserved = 0;
        frame->flags    = flags;
        frame->can_id   = can_id;
        frame->can_dlc  = can_dlc;
        frame->echo_id  = ECHO_RxData;
        memcpy(frame->raw_data, frame_data, 64);

        if (rx_header->FDFormat == FDCAN_FD_CAN)
            frame->pack_FD.timestamp_us = rx_header->RxTimestamp; // 32 bit
        else // classic frame
            frame->pack_classic.timestamp_us = rx_header->RxTimestamp; // 32 bit
    }

    // add the frame to list_to_host with IRQs disabled
    list_add_tail_locked(&obj_to_host->list, &usb_buf->list_to_host);
}

// a CAN packet from the Tx FIFO has been sent and acknowledged on CAN bus --> send marker to host.
// Both protocols retire reserved completion records only after hardware completion.
void buf_store_tx_echo(int channel, FDCAN_TxEventFifoTypeDef* tx_event)
{
    buf_tx_finish(channel, (uint8_t)tx_event->MessageMarker, tx_event->TxTimestamp, true);
}

// append an error frame to the list_to_host
void buf_store_error(int channel)
{
    uint32_t irq_mask = system_irq_save();
    buf_class* usb_buf = buf_get_inst_for_usb(channel);
    
    kHostFrameObject* obj_to_host = buf_get_frame_locked(&usb_buf->list_host_pool);
    if (!obj_to_host)
    {
        system_irq_restore(irq_mask);
        return;
    } // buffer overflow! buf_process() will report this error to the host

    kHostFrameLegacy* frame_gs    = &obj_to_host->frame;
    kErrorElmue*      frame_elmue = (kErrorElmue*)&obj_to_host->frame;
    memset(frame_gs, 0, sizeof(kHostFrameLegacy));

    uint8_t* frame_data;
    if (GLB_ProtoElmue) // new ElmüSoft protocol
        frame_data = frame_elmue->err_data;
    else // legacy Geschwister Schneider protocol
        frame_data = frame_gs->pack_classic.data;

    uint32_t can_id = 0;

    // get errors that are still present after the last error_clear()
    kCanErrorState report;
    error_snapshot(channel, &report);
    kCanErrorState* state = &report;
    switch (state->bus_status)
    {
        case BUS_StatusOff:
            can_id |= ERID_Bus_is_off;
            break;
        case BUS_StatusPassive:
            if (state->tx_err_count >= 128) frame_data[1] |= ER1_Tx_Passive_status_reached;
            if (state->rx_passive || state->rx_err_count >= 128) frame_data[1] |= ER1_Rx_Passive_status_reached;
            break;
        case BUS_StatusWarning: // status Warning may be reported although there are only 60 errors (normally > 96) !!!
            if (state->tx_err_count >= 96) frame_data[1] |= ER1_Tx_Errors_at_warning_level;
            if (state->rx_err_count >= 96 || state->rx_passive) frame_data[1] |= ER1_Rx_Errors_at_warning_level;
            break;
        default:
            if (state->back_to_active) // the bus has returned from a previous Warning, Passive or Off state to Active
                frame_data[1] |= ER1_Bus_is_back_active;
            break;
    }

    switch (state->last_proto_err)
    {
        case FDCAN_PROTOCOL_ERROR_ACK:
            can_id |= ERID_No_ACK_received;
            break;
        case FDCAN_PROTOCOL_ERROR_CRC:
            if (GLB_ProtoElmue) can_id |= ERID_CRC_Error;
            else { can_id |= ERID_Protocol_violation; frame_data[3] = 0x08; }
            break;
        case FDCAN_PROTOCOL_ERROR_STUFF:
            frame_data[2] |= ER2_Bit_stuffing_error;
            break;
        case FDCAN_PROTOCOL_ERROR_FORM:
            frame_data[2] |= ER2_Frame_format_error;
            break;
        case FDCAN_PROTOCOL_ERROR_BIT1:
            frame_data[2] |= ER2_Unable_to_send_recessive_bit;
            break;
        case FDCAN_PROTOCOL_ERROR_BIT0:
            frame_data[2] |= ER2_Unable_to_send_dominant_bit;
            break;
    }

    if (!GLB_ProtoElmue) // legacy mode
    {
        // The host uses the new protocol    --> all the app_flags are sent in byte 5
        // The host uses the legacy protocol --> clone the flags to ID and Byte 1
        // There is no legacy error flag availabe to transmit APP_CanRxFail or APP_CanTxFail.
        if (state->app_flags & APP_CanTxTimeout)  can_id |= ERID_Tx_Timeout;
        if (state->app_flags & (APP_UsbInOverflow | APP_CanRxFail)) frame_data[1] |= ER1_Rx_Buffer_Overflow;
        if (state->app_flags & APP_CanTxOverflow) frame_data[1] |= ER1_Tx_Buffer_Overflow;

        // These flags are useless, the information is already in the bytes 1 and 2,
        // but for compatibility with legacy software they are also set.
        if (frame_data[1] > 0) can_id |= ERID_Controller_problem;
        if (frame_data[2] > 0) can_id |= ERID_Protocol_violation;
    }

    if (!GLB_ProtoElmue) can_id |= 0x0200U; // CAN_ERR_CNT: data[6:7] are valid.
    if (state->restarted) can_id |= ERID_Controller_restarted;

    // Byte 5 was unused in legacy firmware. The new firmware transmits more error details here.
    // The legacy firmware only supported ER1_Rx/Tx_Buffer_Overflow. The app_flags give more details.
    frame_data[5] = state->app_flags;
	frame_data[6] = state->tx_err_count;
	frame_data[7] = state->rx_err_count;

    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        frame_elmue->header.size     = sizeof(kErrorElmue);
        frame_elmue->header.msg_type = MSG_Error;
        frame_elmue->err_id          = can_id; // the flag CAN_ID_Error is not needed as we have MSG_Error
        frame_elmue->timestamp       = system_get_timestamp();

        if ((GLB_UserFlags[channel] & USR_Timestamp) == 0)
            frame_elmue->header.size -= 4;
    }
    else // legacy Geschwister Schneider protocol
    {
        frame_gs->channel = channel;
        frame_gs->echo_id = ECHO_RxData;
        frame_gs->can_id  = can_id | CAN_ID_Error;
        frame_gs->can_dlc = 8;
        frame_gs->pack_classic.timestamp_us = system_get_timestamp();
    }

    // add the frame to list_to_host with IRQs disabled
    list_add_tail_locked(&obj_to_host->list, &usb_buf->list_to_host);
    error_report_commit(channel, &report, HAL_GetTick());
    system_irq_restore(irq_mask);
}

// ---------------------------------------------------------------------------------------------------

// Helper function: Get a frame and remove it from it's list whith IRQs disabled
// returns NULL if the list is empty
kHostFrameObject* buf_get_frame_locked(list_item* list_head)
{
    uint32_t irq_mask = system_irq_save();
    kHostFrameObject* frame_obj = list_get_head_or_null(list_head, kHostFrameObject, list);
    if (!frame_obj)
    {
        system_irq_restore(irq_mask);
        return NULL;
    }
    list_remove(&frame_obj->list); // remove frame_obj from it's list
    system_irq_restore(irq_mask);
    return frame_obj;
}

buf_class* buf_get_instance(int channel)
{
    return &buf_inst[channel];
}

// Although the multi channel firmware creates one USB interface for each CAN channel,
// the legacy protocol routes all traffic of all CAN channels through the first USB interface (EP 81 / 02) for backward compatibility.
// kHostFrameLegacy.channel tells the host which channel is the origin/destination of the packet.
buf_class* buf_get_inst_for_usb(int channel)
{
    if (GLB_ProtoElmue)     
        return &buf_inst[channel]; // ElmüSoft -> send each CAN channel through it's own USB interface 0, 2 or 3
    else
        return &buf_inst[0];       // Legacy   -> send all CAN channels through USB interface 0
}