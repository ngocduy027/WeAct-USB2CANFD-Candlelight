#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Validate one wire record, clear unused bytes and return its CAN channel.
 * destination must provide 80 bytes; input and destination must not overlap. */
bool frame_decode_host(void *destination, const void *input, uint16_t length,
                       bool elmue, uint8_t channel_count, uint8_t *channel);
