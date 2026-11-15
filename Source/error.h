/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "settings.h"

typedef struct 
{
    eErrorBusStatus  bus_status;     // set by hardware
    eErrorAppFlags   app_flags;      // set by firmware
    uint8_t          last_proto_err; // values = 0...7 FDCAN_PROTOCOL_ERROR_xxx
    uint8_t          tx_err_count;   // Tx Error Count (0 ... 255) >= 96 --> warning, >= 128 --> passive, >= 248 --> bus off
    uint8_t          rx_err_count;   // Rx Error Count (0 ... 255) >= 96 --> warning, >= 128 --> passive, >= 248 --> bus off
    bool             restarted;
    bool             rx_passive;
    bool             back_to_active; // the bus has returned from a previous Warning, Passive or Off state to Active
} kCanErrorState;

typedef struct
{
    bool           report_now;
    bool           reported_once;
    bool           bus_off_pending;
    bool           restart_pending;
    uint8_t        protocol_pending;
    kCanErrorState bus_off_state;
    uint32_t       last_tick;
    kCanErrorState cur_state;
    kCanErrorState last_state;
} err_class;

void error_init(int channel);
void error_assert(int channel, eErrorAppFlags flag, bool report_immediately);
bool error_is_report_due(int channel, uint32_t tick_now);
void error_clear(int channel);
kCanErrorState* error_get_state(int channel);




#ifdef Candlelight
void error_update(int channel, const FDCAN_ProtocolStatusTypeDef *status,
                  const FDCAN_ErrorCountersTypeDef *counters);
void error_snapshot(int channel, kCanErrorState *state);
void error_report_commit(int channel, const kCanErrorState *state, uint32_t now);
bool error_busoff_pending(int channel);
#endif
