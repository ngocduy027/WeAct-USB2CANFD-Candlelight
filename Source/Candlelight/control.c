/*
    The MIT License
    Implemenatation of USB GS Class (Geschwister Schneider)
    Copyright (c) 2025 ElmueSoft / Hubert Denkmair
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "can.h"
#include "utils.h"
#include "error.h"
#include "led.h"
#include "dfu.h"
#include "control.h"
#include "usb_class.h"
#include "usb_ioreq.h"

// ----- Globals
extern eUserFlags     GLB_UserFlags[CHANNEL_COUNT];
extern bool           GLB_ProtoElmue;

// ---- Settings  (from settings.h)
extern int            SET_TermPins[CHANNEL_COUNT];

// legacy Geschwister Schneider protocol
kCapabilityClassic    GS_CapabilityClassic;
kCapabilityFD         GS_CapabilityFD;
kDeviceVersion        GS_DeviceVersion = {0};
// new ELmüSoft protocol
kBoardInfo            ELM_BoardInfo    = {0};
eFeedback             ELM_LastError    = FBK_Success;

// Buffer for Endpoint 0 data (SETUP requests with Flash Write Data of 2 kB)
// This buffer contains OUT data from the host in the second stage of SETUP requests.
uint8_t __aligned(4)  ep0_buf[MAX(USB_MAX_EP0_SIZE, MAX_FLASH_DATA_LEN + 8)];

// SETUP requests with OUT data are executed in two stages,
// the first stage uses this variable to pass the request to the second stage.
USBD_SetupReqTypedef  last_setup_request = {0};

// Only the main loop executes state-changing control commands.
static volatile bool command_pending;
static volatile uint32_t command_generation;
static USBD_SetupReqTypedef command_request;
static uint8_t __aligned(8) command_buf[sizeof(ep0_buf)];
static eFeedback control_execute_OUT_data(USBD_HandleTypeDef *pdev);

void control_cancel_pending(void)
{
    command_pending = false;
    ++command_generation;
}

void control_process_pending(USBD_HandleTypeDef *pdev)
{
    uint32_t mask = system_irq_save();
    if (!command_pending || pdev->dev_state != USBD_STATE_CONFIGURED)
    {
        command_pending = false;
        system_irq_restore(mask);
        return;
    }
    uint32_t generation = command_generation;
    command_request = last_setup_request;
    memcpy(command_buf, ep0_buf, command_request.wLength);
    command_pending = false;
    system_irq_restore(mask);

    eFeedback result = control_execute_OUT_data(pdev);
    mask = system_irq_save();
    ELM_LastError = result;
    if (generation == command_generation && pdev->dev_state == USBD_STATE_CONFIGURED &&
        pdev->ep0_state == USBD_EP0_DATA_OUT)
    {
        if (result == FBK_Success) USBD_CtlSendStatus(pdev);
        else USBD_CtlError(pdev, &pdev->request);
    }
    system_irq_restore(mask);
}

void control_setup_OUT_data(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
    command_pending = true;
}

// called from the main loop
void control_init()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        // all the other flags must be enabled by the user
        GLB_UserFlags[C] = USR_CandleDefault;
    }

    GS_DeviceVersion.sw_version_bcd = FIRMWARE_VERSION_BCD; // BCD version 0x250814 --> display as "25.08.14" (14th august 2025)
    GS_DeviceVersion.hw_version_bcd = 0x200;                // BCD version 0x200    --> display as  "2.00" (hardware = CANable 2.0)
    GS_DeviceVersion.icount         = CHANNEL_COUNT - 1;    // interface count - 1

    // ------------------------------------------------

    GS_CapabilityClassic.feature = GS_DevFlagListenOnly     |
                                   GS_DevFlagLoopback       |
                                   GS_DevFlagOneShot        |
                                   GS_DevFlagTimestamp      |
                                   GS_DevFlagIdentify       |
                                   GS_DevFlagCAN_FD         |
                                   GS_DevFlagBitTimingFD    |
                                   ELM_DevFlagProtocolElmue |
                                   ELM_DevFlagDisableTxEcho;
#ifdef LED_CAN_ACTIVITY_ONLY
    GS_CapabilityClassic.feature &= ~GS_DevFlagIdentify;
#endif
    if (SET_TermPins[0] >= 0)
        GS_CapabilityClassic.feature |= GS_DevFlagTermination;

    // ------------------------------------------------

    // store the REAL limits of the processor, not totally wrong values as in the legacy firmware from Hubert Denkmair
    bitlimits* limits = utils_get_bit_limits();

    GS_CapabilityClassic.fclk_can      = system_get_can_clock();
    GS_CapabilityClassic.time.seg1_min = 1;
    GS_CapabilityClassic.time.seg1_max = limits->nom_seg1_max;
    GS_CapabilityClassic.time.seg2_min = 1;
    GS_CapabilityClassic.time.seg2_max = limits->nom_seg2_max;
    GS_CapabilityClassic.time.brp_min  = 1;
    GS_CapabilityClassic.time.brp_max  = limits->nom_brp_max;
    GS_CapabilityClassic.time.brp_inc  = 1;
    GS_CapabilityClassic.time.sjw_max  = limits->nom_sjw_max;

    // ------------------------------------------------

    GS_CapabilityFD.fclk_can = GS_CapabilityClassic.fclk_can;
    GS_CapabilityFD.feature  = GS_CapabilityClassic.feature;
    GS_CapabilityFD.time_nom = GS_CapabilityClassic.time;

    GS_CapabilityFD.time_data.seg1_min = 1;
    GS_CapabilityFD.time_data.seg1_max = limits->fd_seg1_max;
    GS_CapabilityFD.time_data.seg2_min = 1;
    GS_CapabilityFD.time_data.seg2_max = limits->fd_seg2_max;
    GS_CapabilityFD.time_data.brp_min  = 1;
    GS_CapabilityFD.time_data.brp_max  = limits->fd_brp_max;
    GS_CapabilityFD.time_data.brp_inc  = 1;
    GS_CapabilityFD.time_data.sjw_max  = limits->fd_sjw_max;

    // -------------- Added by ElmüSoft ----------------

    ELM_BoardInfo.McuDeviceID = (uint16_t)HAL_GetDEVID();
    strcpy(ELM_BoardInfo.McuName,   utils_get_MCU_name()); // "STM32G431"  (from makefile)
    strcpy(ELM_BoardInfo.BoardName, TARGET_BOARD);         // "Multiboard", "OpenlightLabs", "Jhoinrch"  (from makefile)

    if (system_using_quartz()) ELM_BoardInfo.BoardFlags |= BRD_Quartz_In_Use;
}

// A SETUP vendor request packet has been received (first stage).
// For IN  data requests (to the host) send the response.
// For OUT data requests (from the host) provide a buffer which will be filled and passed to control_vendor_OUT_data()
// See "USB Tutorial.chm" in subfolder "Documentation"
// This function is called from HAL_PCD_SetupStageCallback() -> USBD_LL_SetupStage() -> USBD_StdDevReq() -> USBD_GS_Setup() -> USBD_GS_Vendor_Request()
// returns false on error and sets ELM_LastError.
// IMPORTANT: Read the comment for USBD_GS_Vendor_Request()
bool control_setup_request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    // GetLastError always sends a valid response even if the ElmüSoft protocol is not enabled or the host sends an invalid channel.
    if (req->bRequest == ELM_ReqGetLastError)
    {
        if ((req->bRequestType & USB_REQ_DIRECTION_MASK) != USB_REQ_DIRECTION_IN || req->wLength == 0)
            return false;
        uint8_t last_err = ELM_LastError;
        USBD_CtlSendData(pdev, &last_err, 1);
        return true;
    }

    // To enable the new ElmüSoft commands the host must send as the first command:
    // GS_ReqSetDeviceMode with GS_ModeReset and ELM_DevFlagProtocolElmue.
    if (req->bRequest >= ELM_ReqFIRST && !GLB_ProtoElmue)
    {
        ELM_LastError = FBK_InvalidCommand;
        return false; // stall endpoint 0
    }

    // -------- handle wValue -----------

    int      channel    = 0;
    ePinID   pin_id     = 0; // invalid
    uint32_t flash_addr = 0; // invalid
    switch (req->bRequest)
    {
        // req->wValue is the pin ID
        case ELM_ReqGetPinStatus:
            pin_id = req->wValue;
            break;

        // req->wValue is the flash segment
        case ELM_ReqReadFlash:
        case ELM_ReqWriteFlash:
            flash_addr = system_get_flash_addr(req->wValue);
            if (flash_addr == 0) // invalid segment
            {
                ELM_LastError = FBK_ParamOutOfRange; // segment is occupied by firmware
                return false; // stall endpoint 0
            }
            break;

        // for per-device messages req->wValue is ignored (the Linux driver sets wValue = 1)
        case GS_ReqSetHostFormat:
        case GS_ReqGetDeviceVersion:
        case GS_ReqGetTimestamp:
        case ELM_ReqSetPinStatus:
        case ELM_ReqGetBoardInfo:
            // if the channel is valid, use it for flashing the correct LED, otherwise fix it.
            channel = MIN(req->wValue, CHANNEL_COUNT - 1);
            break;

        // for per-channel messages req->wValue defines the channel index which must be valid.
        default:
            channel = req->wValue;
            if (channel >= CHANNEL_COUNT)
            {
                ELM_LastError = FBK_InvalidParameter;
                return false; // stall endpoint 0
            }
            break;
    }

#ifndef LED_CAN_ACTIVITY_ONLY
    // If the channel is closed the Rx LED indicates that a SETUP command was recived.
    // If the channel is open   the Rx LED indicates that a CAN packet was received.
    if (!can_is_open(channel))
        led_flash_RX(channel); // Flash the blue Rx LED shortly for 15 ms
#endif

    ELM_LastError = FBK_Success; // reset error from the last command

    // ------- OUT: Host -> Device (error checking in next function) --------

    if ((req->bRequestType & USB_REQ_DIRECTION_MASK) == USB_REQ_DIRECTION_OUT)
    {
        if (pdev->dev_state != USBD_STATE_CONFIGURED)
        {
            ELM_LastError = FBK_InvalidCommand;
            return false;
        }
        uint16_t min_len;

        switch (req->bRequest)
        {
            case GS_ReqSetHostFormat:
                min_len = sizeof(uint32_t);
                break;
            case GS_ReqIdentify:
#ifdef LED_CAN_ACTIVITY_ONLY
                ELM_LastError = FBK_UnsupportedFeature;
                return false;
#endif
                min_len = sizeof(uint32_t); // the application sends a 32 bit "mode", but the value is ignored here
                break;
            case GS_ReqSetBitTiming:
            case GS_ReqSetBitTimingFD:
                min_len = sizeof(kBitTiming);
                break;
            case GS_ReqSetDeviceMode:
                min_len = sizeof(kDeviceMode);
                break;
            case GS_ReqSetTermination:
                min_len = sizeof(uint32_t);
                break;
            case ELM_ReqSetFilter:
                min_len = sizeof(kFilter);
                break;
            case ELM_ReqSetBusLoadReport:
                min_len = sizeof(uint8_t);
                break;
            case ELM_ReqSetPinStatus:
                min_len = sizeof(kPinStatus);
                break;
            case ELM_ReqWriteFlash:
                if (req->wLength > MAX_FLASH_DATA_LEN)
                {
                    ELM_LastError = FBK_ParamOutOfRange;
                    return false; // stall endpoint 0
                }
                min_len = req->wLength;
                break;
            default:
                ELM_LastError = FBK_InvalidCommand;
                return false; // stall endpoint 0
        }

        if (req->wLength != min_len || req->wLength > sizeof(ep0_buf))
        {
            // host has sent incomplete OUT data
            ELM_LastError = FBK_InvalidParameter;
            return false; // stall endpoint 0
        }

        // provide the buffer ep0_buf in which the OUT data from the host is passed to control_setup_OUT_data()
        last_setup_request = *req;
        memset(ep0_buf, 0, sizeof(ep0_buf));
        if (req->wLength == 0)
        {
            pdev->ep0_state = USBD_EP0_DATA_OUT;
            command_pending = true;
        }
        else USBD_CtlPrepareRx(pdev, ep0_buf, req->wLength);
        return true;
    }
    else  // -------- IN: Device -> Host (error checking here) --------
    {
        if (req->wLength == 0) return false;
        uint16_t value16;
        uint32_t value32;
        void*    src;
        uint16_t len;

        switch (req->bRequest)
        {
            case GS_ReqGetCapabilities:   // channel ignored, but must be valid
                src = &GS_CapabilityClassic;
                len = sizeof(kCapabilityClassic);
                break;
            case GS_ReqGetCapabilitiesFD: // channel ignored, but must be valid
                src = &GS_CapabilityFD;
                len = sizeof(kCapabilityFD);
                break;
            case GS_ReqGetDeviceVersion:
                src = &GS_DeviceVersion;
                len = sizeof(kDeviceVersion);
                break;
            case GS_ReqGetTimestamp:
                // Bugfix: The legacy firmware used a timestamp created only when a USB SOF packet was received.
                // This is totally stupid, because the timestamp has a precision of 1 µs, but SOF packets are received once every millisecond.
                value32 = system_get_timestamp();
                src = &value32;
                len = sizeof(uint32_t);
                break;
            case GS_ReqGetTermination:
            {
                bool bEnabled;
                if (!can_get_termination(channel, &bEnabled))
                {
                    ELM_LastError = FBK_UnsupportedFeature;
                    return false; // the board cannot switch on/off the termination resistor
                }
                value32 = bEnabled ? GS_TerminationON : GS_TerminationOFF;
                src = &value32;
                len = sizeof(uint32_t);
                break;
            }
            case ELM_ReqGetBoardInfo:
                src = &ELM_BoardInfo;
                len = sizeof(kBoardInfo);
                break;
            case ELM_ReqGetPinStatus:
                // ePinID must be transmitted in wValue
                // Normally wValue transmits the channel index, but processor pins do not depend on channels.
                switch (pin_id)
                {
                    case PINID_BOOT0: // currently the only implemented pin (PINST_High is irrelevant here)
                        value16 = system_is_option_enabled(OPT_BOOT0_Enable) ? PINST_Enabled : 0;
                        break;
                    default:
                        ELM_LastError = FBK_InvalidParameter;
                        return false;
                }
                src = &value16;
                len = sizeof(uint16_t);
                break;

            case ELM_ReqReadFlash:
                // The first 2 bytes of the flash segment contain the length of the data
                src = (void*)(flash_addr + 2);
                len = ((uint16_t*)flash_addr)[0];
                
                if (len == 0xFFFF) len = 0; // erased segment
                len = MIN(len, MAX_FLASH_DATA_LEN);
                break;

            default:
                ELM_LastError = FBK_InvalidCommand;
                return false; // stall endpoint 0
        }

        // If the host passes a buffer that is too small for the entire response, this is not an error.
        // All USB devices return a partial response in this case.
        // If the host passes a buffer that is bigger than the response, only the response size is sent.
        len = MIN(len, req->wLength);

        // return the requested IN data to the host
        USBD_CtlSendData(pdev, (uint8_t*)src, len);
        return true;
    }
}

// Execute a validated command from the main loop. The status stage remains NAKed
// until completion, allowing a failed command to return a real USB STALL.
static eFeedback control_execute_OUT_data(USBD_HandleTypeDef *pdev)
{
    eFeedback result = FBK_Success;
    int channel = command_request.wValue; // wValue has already been checked to be valid.

    switch (command_request.bRequest)
    {
        case GS_ReqSetHostFormat:
        {
            // The firmware of the original USB2CAN by Geschwister Schneider exchanges all data in host byte order.
            // The application sends the value 0xbeef indicating the desired byte order.
            // The widely used open source CandleLight does not support this and uses always little endian byte order.
            // return an error if the host requests big endian.
            uint32_t* format = (uint32_t*)command_buf;
            if (*format != 0xbeef)
                result = FBK_UnsupportedFeature;
            return result;
        }
        case GS_ReqSetBitTiming: // set CAN classic and CAN FD nominal baudrate + samplepoint
        {
            kBitTiming* timing = (kBitTiming*)command_buf;
            if (timing->prop > UINT32_MAX - timing->seg1)
            {
                result = FBK_ParamOutOfRange;
                return result;
            }
            result = can_set_nom_bit_timing(channel, timing->brp, timing->prop + timing->seg1, timing->seg2, timing->sjw);
            return result;
        }
        case GS_ReqSetBitTimingFD: // set CAN FD data baudrate + samplepoint
        {
            kBitTiming* timing = (kBitTiming*)command_buf;
            if (timing->prop > UINT32_MAX - timing->seg1)
            {
                result = FBK_ParamOutOfRange;
                return result;
            }
            result = can_set_data_bit_timing(channel, timing->brp, timing->prop + timing->seg1, timing->seg2, timing->sjw);
            return result;
        }
        case GS_ReqSetDeviceMode:
        {
            // ------------------------- 1.) Error Checking --------------------------------

            kDeviceMode* dev_Mode = (kDeviceMode*)command_buf;
            if (dev_Mode->mode != GS_ModeStart && dev_Mode->mode != GS_ModeReset)
            {
                result = FBK_InvalidParameter;
                return result;
            }
            if (dev_Mode->mode == GS_ModeStart)
            {
                if (can_is_open(channel))
                {
                    result = FBK_AdapterMustBeClosed;
                    return result;
                }
                result = can_select_fd_mode(channel, (dev_Mode->flags & GS_DevFlagCAN_FD) != 0);
                if (result != FBK_Success) return result;
            }

            uint32_t supported = GS_DevFlagListenOnly | GS_DevFlagLoopback | GS_DevFlagOneShot |
                GS_DevFlagTimestamp | GS_DevFlagCAN_FD | ELM_DevFlagProtocolElmue | ELM_DevFlagDisableTxEcho;
            if (dev_Mode->flags & ~supported) return FBK_UnsupportedFeature;
            bool requested_elmue = (dev_Mode->flags & ELM_DevFlagProtocolElmue) != 0;
            if (dev_Mode->mode == GS_ModeStart && can_is_any_open() && requested_elmue != GLB_ProtoElmue)
                return FBK_AdapterMustBeClosed;
            if (dev_Mode->mode == GS_ModeReset) can_close(channel);
            if (!can_is_any_open() && requested_elmue != GLB_ProtoElmue)
            {
                USBD_ProtocolBoundary();
                GLB_ProtoElmue = requested_elmue;
            }
            if (dev_Mode->mode == GS_ModeStart)
            {
                USBD_ChannelBoundary(channel);
                buf_clear_host_buffer(channel);
                buf_clear_can_buffer(channel);
            }

            GLB_UserFlags[channel] = USR_CandleDefault; // reset channel flags to their default
            if (dev_Mode->flags &  GS_DevFlagOneShot)       GLB_UserFlags[channel] &= ~USR_Retransmit;
            if (dev_Mode->flags &  GS_DevFlagTimestamp)     GLB_UserFlags[channel] |=  USR_Timestamp;
            if (dev_Mode->flags & ELM_DevFlagDisableTxEcho) GLB_UserFlags[channel] &= ~USR_ReportTX;


            if (GLB_ProtoElmue)
            {
                for (int C=0; C<CHANNEL_COUNT; C++)
                {
                    GLB_UserFlags[C] |= USR_DebugReport;
                }
            }

            // ------------------------- 3.) Start / Reset ----------------------------------

            if (dev_Mode->mode == GS_ModeStart)
            {
                uint32_t open_mode = FDCAN_MODE_NORMAL;
                if ((dev_Mode->flags & GS_DevFlagListenOnly) > 0)
                {
                    open_mode = FDCAN_MODE_BUS_MONITORING; // Do not send ACK to the CAN bus

                    if ((dev_Mode->flags & GS_DevFlagLoopback) > 0)
                        open_mode = FDCAN_MODE_INTERNAL_LOOPBACK; // Do not send neither ACK nor packets to the CAN bus, Loopback Tx -> Rx
                }
                else
                {
                    if ((dev_Mode->flags & GS_DevFlagLoopback) > 0)
                        open_mode = FDCAN_MODE_EXTERNAL_LOOPBACK; // Send packets to CAN bus and Loopback Tx -> Rx
                }
                result = can_open(channel, open_mode);
                return result;
            }
            if (dev_Mode->mode == GS_ModeReset)
            {
                return FBK_Success;
            }
        }
        case GS_ReqIdentify:
        {
#ifdef LED_CAN_ACTIVITY_ONLY
            return FBK_UnsupportedFeature;
#endif
            uint32_t* mode = (uint32_t*)command_buf; // 1 = blink, 0 = stop
            led_blink_identify(channel, *mode); // blink blue / green LEDs alternatingly
            return result;
        }
        case GS_ReqSetTermination:
        {
            uint32_t* termination = (uint32_t*)command_buf; // eTermination
            if (!can_set_termination(channel, *termination == GS_TerminationON))
                result = FBK_UnsupportedFeature;
            return result;
        }
        case ELM_ReqSetFilter:
        {
            kFilter* filter = (kFilter*)command_buf;
            switch (filter->Operation)
            {
                case FIL_ClearAll:
                    result = can_clear_filters(channel);
                    return result;
                case FIL_AcceptMask11bit:
                case FIL_AcceptMask29bit:
                    result = can_set_mask_filter(channel, filter->Operation == FIL_AcceptMask29bit, filter->Filter, filter->Mask);
                    return result;
                default:
                    result = FBK_InvalidParameter;
                    return result;
            }
        }
        case ELM_ReqSetBusLoadReport:
        {
            uint8_t interval = command_buf[0];
            result = can_enable_busload(channel, interval); // interval in 100 ms steps
            return result;
        }
        case ELM_ReqSetPinStatus:
        {
            kPinStatus* pin_status = (kPinStatus*)command_buf;

            // Enabling the pin needs not to be implemented here.
            // The pin is automatically enabled when entering DFU mode in dfu_switch_to_bootloader()
            if (pin_status->PinID == PINID_BOOT0 && pin_status->Operation == PINOP_Disable)
            {
                result = system_set_option_bytes(OPT_BOOT0_Disable);
                return result;
            }
            result = FBK_InvalidParameter;
            return result;
        }
        case ELM_ReqWriteFlash:
        {
            result = system_write_flash(command_request.wValue, command_buf, command_request.wLength);
            return result;
        }
    }
    return FBK_InvalidCommand;
}

// ========================= Errors ===========================

// This function is called approx 100 times in one millisecond from the main loop
// if the error state has changed, report it every 100 ms
// if the error state did not change, report the same state only every 3000 ms.
void control_process(int channel, uint32_t tick_now)
{
    if (error_is_report_due(channel, tick_now))
        buf_store_error(channel);

    // Revover BusOff AFTER printing error BusOff to the Trace output!
    can_recover_bus_off(channel);
}

void control_report_busload(int channel, uint8_t busload_percent)
{
    // only called for ElmüSoft protocol
    buf_class* usb_buf = buf_get_instance(channel);

    kHostFrameObject* obj_to_host = buf_get_frame_locked(&usb_buf->list_host_pool);
    if (!obj_to_host)
        return; // buffer overflow! buf_process() will report this error to the host

    kBusloadElmue* packet = (kBusloadElmue*)&obj_to_host->frame;
    packet->header.size     = sizeof(kBusloadElmue);
    packet->header.msg_type = MSG_Busload;
    packet->bus_load        = busload_percent;

    list_add_tail_locked(&obj_to_host->list, &usb_buf->list_to_host);
}

// Send a debug message. Maximum length is 78 characters.
// The message may contain "\n" for multi-line output.
// To make sure that you see all debug output the first command that you execute on each channel
// should be GS_ReqSetDeviceMode with GS_ModeReset and ELM_DevFlagProtocolElmue.
// This closes the device if still open and enables debug output.
// If the device has a legacy firmware it will ignore any flags that are passed with GS_ModeReset.
// Only the new ElmüSoft firmware allows to set flags when closing the device.
bool control_send_debug_mesg(int channel, const char* message)
{
    // USR_DebugReport is only set if GLB_ProtoElmue == true
    if ((GLB_UserFlags[channel] & USR_DebugReport) == 0)
        return false;

    // only called for ElmüSoft protocol
    buf_class* usb_buf = buf_get_instance(channel);

    kHostFrameObject* obj_to_host = buf_get_frame_locked(&usb_buf->list_host_pool);
    if (!obj_to_host)
        return false; // buffer overflow! buf_process() will report this error to the host

    // ------------------------------

    int len = strlen(message);
    if (len > sizeof(kHostFrameLegacy) - sizeof(kStringElmue))
    {
        message = "*** Dbg msg too long"; // 20 chars
        len = 20;
    }

    kStringElmue* packet = (kStringElmue*)&obj_to_host->frame;
    packet->header.size     = sizeof(kStringElmue) + len;
    packet->header.msg_type = MSG_String;
    memcpy(packet->ascii_msg, message, len);

    list_add_tail_locked(&obj_to_host->list, &usb_buf->list_to_host);
    return true;
}
