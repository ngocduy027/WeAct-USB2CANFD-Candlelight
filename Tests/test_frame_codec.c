#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#include "candlelight_def.h"
#include "frame_codec.h"

int main(void)
{
    struct { uint8_t before[16]; kHostFrameLegacy frame; uint8_t after[16]; } guarded;
    uint8_t source[128];
    for (unsigned length = 0; length <= 128; ++length)
    {
        memset(source, 0, sizeof(source));
        kHostFrameLegacy *input = (void *)source;
        input->can_id = 0x123;
        input->can_dlc = 8;
        memset(&guarded, 0xCC, sizeof(guarded));
        uint8_t channel = 0;
        bool valid = frame_decode_host(&guarded.frame, source, length, false, 1, &channel);
        assert(valid == (length == 20 || length == 24 || length == 76 || length == 80));
        for (unsigned i = 0; i < 16; ++i)
            assert(guarded.before[i] == 0xCC && guarded.after[i] == 0xCC);
    }
    for (unsigned dlc = 0; dlc <= 255; ++dlc)
    {
        memset(source, 0, sizeof(source));
        kHostFrameLegacy *input = (void *)source;
        input->can_id = 0x123; input->can_dlc = dlc;
        uint8_t channel = 0;
        assert(frame_decode_host(&guarded.frame, source, 20, false, 1, &channel) == (dlc <= 15));
    }
    for (unsigned length = 0; length <= 80; ++length)
    {
        memset(source, 0xAA, sizeof(source));
        kTxFrameElmue *input = (void *)source;
        input->header.size = length; input->header.msg_type = MSG_TxFrame;
        input->can_id = 0x123; input->flags = FRM_FDF; input->marker = 7;
        uint8_t channel = 0;
        bool valid = frame_decode_host(&guarded.frame, source, length, true, 1, &channel);
        assert(valid == (length >= 8 && length <= 72));
        if (valid)
            for (unsigned i = length; i < 80; ++i)
                assert(((uint8_t *)&guarded.frame)[i] == 0);
    }
    memset(source, 0, sizeof(source));
    kHostFrameLegacy *input = (void *)source;
    uint8_t channel = 0;
    input->can_id = CAN_ID_Error | 0x123;
    assert(!frame_decode_host(&guarded.frame, source, 20, false, 1, &channel));
    input->can_id = 0x800;
    assert(!frame_decode_host(&guarded.frame, source, 20, false, 1, &channel));
    input->can_id = CAN_ID_29Bit | CAN_MASK_29;
    assert(frame_decode_host(&guarded.frame, source, 20, false, 1, &channel));
    input->channel = 1;
    assert(!frame_decode_host(&guarded.frame, source, 20, false, 1, &channel));
    input->channel = 0; input->flags = FRM_FDF; input->can_id = CAN_ID_RTR | 0x123;
    assert(!frame_decode_host(&guarded.frame, source, 76, false, 1, &channel));
    input->flags = FRM_BRS; input->can_id = 0x123;
    assert(!frame_decode_host(&guarded.frame, source, 20, false, 1, &channel));
    return 0;
}
