#ifndef NOTE4_PROVISIONING_STATE_H
#define NOTE4_PROVISIONING_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_config.h"

typedef enum {
    NOTE4_PROVISIONING_BOOT = 0,
    NOTE4_PROVISIONING_CONFIGURING,
    NOTE4_PROVISIONING_DISCARD_DIALOG,
    NOTE4_PROVISIONING_APPLYING,
    NOTE4_PROVISIONING_CONNECTING,
    NOTE4_PROVISIONING_CONNECTED,
    NOTE4_PROVISIONING_FAILURE,
} note4_provisioning_state_t;

typedef enum {
    NOTE4_PROVISIONING_EVENT_NO_CONFIG = 0,
    NOTE4_PROVISIONING_EVENT_CONFIG_LOADED,
    NOTE4_PROVISIONING_EVENT_LONG_DOWN,
    NOTE4_PROVISIONING_EVENT_OK,
    NOTE4_PROVISIONING_EVENT_CANCEL,
    NOTE4_PROVISIONING_EVENT_COMMIT_ACCEPTED,
    NOTE4_PROVISIONING_EVENT_COMMIT_FAILED,
    NOTE4_PROVISIONING_EVENT_GOT_IP,
    NOTE4_PROVISIONING_EVENT_CONNECT_FAILED,
    NOTE4_PROVISIONING_EVENT_RETRY,
    NOTE4_PROVISIONING_EVENT_COMMIT_SAVED,
} note4_provisioning_event_type_t;

typedef struct {
    note4_provisioning_event_type_t type;
    size_t slot;
} note4_provisioning_event_t;

typedef struct {
    note4_provisioning_state_t state;
    size_t slot;
    uint8_t round;
    uint8_t configured_mask;
    uint8_t attempted_mask;
    bool has_committed_config;
    bool has_staged_config;
    bool storage_error;
    bool pairing;
    uint32_t passkey;
    uint8_t magpie_phase;
    uint8_t magpie_result;
    bool magpie_error_page;
    bool magpie_reading;
    uint32_t magpie_retry_seconds;
    char display_timezone[64];
    uint8_t display_layout;
    bool adaptive_font;
} note4_provisioning_snapshot_t;

void note4_provisioning_init(note4_provisioning_snapshot_t *snapshot);
void note4_provisioning_apply(note4_provisioning_snapshot_t *snapshot,
                              const note4_provisioning_event_t *event,
                              uint8_t configured_mask);

#endif
