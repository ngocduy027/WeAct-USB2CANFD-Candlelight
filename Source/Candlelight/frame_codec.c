#include "frame_codec.h"
#include <string.h>
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
#include "candlelight_def.h"

static unsigned fd_length(unsigned dlc)
{
    static const uint8_t lengths[16] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    return lengths[dlc];
}

bool frame_decode_host(void *destination, const void *input, uint16_t length,
                       bool elmue, uint8_t channel_count, uint8_t *channel)
{
    uint32_t id;
    uint8_t flags;
    if (!input || !destination || !channel || !channel_count ||
        length > sizeof(kHostFrameLegacy)) return false;
    if (elmue)
    {
        if (*channel >= channel_count || length < sizeof(kTxFrameElmue)) return false;
        const kTxFrameElmue *frame = input;
        if (frame->header.msg_type != MSG_TxFrame || frame->header.size != length) return false;
        id = frame->can_id;
        flags = frame->flags;
        unsigned count = length - sizeof(*frame);
        if (count > 64 || (!(flags & FRM_FDF) && count > 8)) return false;
        if (id & CAN_ID_RTR)
        {
            if (count > 1 || (count == 1 && frame->data_start[0] > 15)) return false;
        }
    }
    else
    {
        if (length != 20 && length != 24 && length != 76 && length != 80) return false;
        const kHostFrameLegacy *frame = input;
        if (frame->channel >= channel_count || frame->can_dlc > 15) return false;
        *channel = frame->channel;
        id = frame->can_id;
        flags = frame->flags;
        unsigned payload = (flags & FRM_FDF) ? fd_length(frame->can_dlc) :
                           (frame->can_dlc > 8 ? 8 : frame->can_dlc);
        if (!(id & CAN_ID_RTR) && length < 12 + payload) return false;
        if ((flags & FRM_FDF) && length < 76) return false;
    }
    if ((id & CAN_ID_Error) ||
        (!(id & CAN_ID_29Bit) && (id & CAN_MASK_29) > CAN_MASK_11) ||
        (flags & ~(FRM_FDF | FRM_BRS | FRM_ESI)) ||
        (!(flags & FRM_FDF) && (flags & (FRM_BRS | FRM_ESI))) ||
        ((flags & FRM_FDF) && (id & CAN_ID_RTR))) return false;

    /* In particular, rounded FD lengths must transmit deterministic zero padding. */
    memset(destination, 0, sizeof(kHostFrameLegacy));
    memcpy(destination, input, length);
    return true;
}
