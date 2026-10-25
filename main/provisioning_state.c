#include "provisioning_state.h"

static bool next_slot(note4_provisioning_snapshot_t *snapshot,
                      uint8_t configured_mask, bool *wrapped) {
    if (wrapped != NULL) *wrapped = false;
    for (size_t offset = 1U; offset <= NOTE4_WIFI_MAX_SLOTS; ++offset) {
        const size_t index = (snapshot->slot + offset) % NOTE4_WIFI_MAX_SLOTS;
        if (configured_mask & (uint8_t)(1U << index)) {
            if (wrapped != NULL && index <= snapshot->slot) *wrapped = true;
            snapshot->slot = index;
            return true;
        }
    }
    return false;
}

static size_t first_slot(uint8_t mask) {
    for (size_t slot = 0U; slot < NOTE4_WIFI_MAX_SLOTS; ++slot) {
        if (mask & (1U << slot)) return slot;
    }
    return 0U;
}

void note4_provisioning_init(note4_provisioning_snapshot_t *snapshot) {
    if (snapshot == NULL) return;
    *snapshot = (note4_provisioning_snapshot_t){
        .state = NOTE4_PROVISIONING_BOOT,
        .round = 1U,
    };
}

void note4_provisioning_apply(note4_provisioning_snapshot_t *snapshot,
                              const note4_provisioning_event_t *event,
                              uint8_t configured_mask) {
    if (snapshot == NULL || event == NULL) return;
    snapshot->configured_mask = configured_mask;
    if (event->type == NOTE4_PROVISIONING_EVENT_CONFIG_LOADED ||
        event->type == NOTE4_PROVISIONING_EVENT_NO_CONFIG ||
        event->type == NOTE4_PROVISIONING_EVENT_LONG_DOWN ||
        event->type == NOTE4_PROVISIONING_EVENT_GOT_IP ||
        (event->type == NOTE4_PROVISIONING_EVENT_OK &&
         (snapshot->state == NOTE4_PROVISIONING_DISCARD_DIALOG ||
          snapshot->state == NOTE4_PROVISIONING_FAILURE))) snapshot->attempted_mask = 0U;
    switch (event->type) {
    case NOTE4_PROVISIONING_EVENT_NO_CONFIG:
        snapshot->state = NOTE4_PROVISIONING_CONFIGURING;
        snapshot->has_committed_config = false;
        snapshot->has_staged_config = true;
        snapshot->round = 1U;
        break;
    case NOTE4_PROVISIONING_EVENT_CONFIG_LOADED:
        snapshot->has_committed_config = configured_mask != 0U;
        snapshot->has_staged_config = false;
        snapshot->round = 1U;
        snapshot->slot = first_slot(configured_mask);
        snapshot->state = configured_mask == 0U
            ? NOTE4_PROVISIONING_CONFIGURING
            : NOTE4_PROVISIONING_CONNECTING;
        break;
    case NOTE4_PROVISIONING_EVENT_LONG_DOWN:
        snapshot->state = NOTE4_PROVISIONING_CONFIGURING;
        snapshot->has_staged_config = true;
        break;
    case NOTE4_PROVISIONING_EVENT_OK:
        if (snapshot->state == NOTE4_PROVISIONING_CONFIGURING) {
            snapshot->state = NOTE4_PROVISIONING_DISCARD_DIALOG;
        } else if (snapshot->state == NOTE4_PROVISIONING_DISCARD_DIALOG) {
            snapshot->state = snapshot->has_committed_config
                ? NOTE4_PROVISIONING_CONNECTING
                : NOTE4_PROVISIONING_BOOT;
            snapshot->slot = first_slot(configured_mask);
            snapshot->round = 1U;
            snapshot->has_staged_config = false;
        } else if (snapshot->state == NOTE4_PROVISIONING_FAILURE) {
            snapshot->state = NOTE4_PROVISIONING_CONNECTING;
            snapshot->round = 1U;
            snapshot->slot = first_slot(configured_mask);
        }
        break;
    case NOTE4_PROVISIONING_EVENT_CANCEL:
        if (snapshot->state == NOTE4_PROVISIONING_DISCARD_DIALOG) {
            snapshot->state = NOTE4_PROVISIONING_CONFIGURING;
        }
        break;
    case NOTE4_PROVISIONING_EVENT_COMMIT_ACCEPTED:
        if (snapshot->state == NOTE4_PROVISIONING_CONFIGURING) {
            snapshot->state = NOTE4_PROVISIONING_APPLYING;
            snapshot->has_staged_config = true;
        }
        break;
    case NOTE4_PROVISIONING_EVENT_COMMIT_SAVED:
        if (snapshot->state == NOTE4_PROVISIONING_APPLYING) {
            snapshot->state = NOTE4_PROVISIONING_CONFIGURING;
            snapshot->has_committed_config = configured_mask != 0U;
            snapshot->has_staged_config = false;
        }
        break;
    case NOTE4_PROVISIONING_EVENT_COMMIT_FAILED:
        if (snapshot->state == NOTE4_PROVISIONING_APPLYING) {
            snapshot->state = NOTE4_PROVISIONING_CONFIGURING;
        }
        break;
    case NOTE4_PROVISIONING_EVENT_GOT_IP:
        if (snapshot->state == NOTE4_PROVISIONING_CONNECTING) {
            snapshot->state = NOTE4_PROVISIONING_CONNECTED;
            snapshot->has_staged_config = false;
        }
        break;
    case NOTE4_PROVISIONING_EVENT_CONNECT_FAILED:
        if (snapshot->state == NOTE4_PROVISIONING_CONNECTED) {
            snapshot->state = NOTE4_PROVISIONING_CONNECTING;
            snapshot->round = 1U;
            (void)next_slot(snapshot, configured_mask, NULL);
            break;
        }
        if (snapshot->state == NOTE4_PROVISIONING_CONNECTING) {
            snapshot->attempted_mask |= (uint8_t)(1U << snapshot->slot);
            (void)next_slot(snapshot, configured_mask, NULL);
            if ((snapshot->attempted_mask & configured_mask) == configured_mask) {
                snapshot->attempted_mask = 0U;
                if (snapshot->round >= 3U) {
                    snapshot->state = NOTE4_PROVISIONING_FAILURE;
                    snapshot->round = 3U;
                } else {
                    ++snapshot->round;
                }
            }
        }
        break;
    case NOTE4_PROVISIONING_EVENT_RETRY:
        if (snapshot->state == NOTE4_PROVISIONING_CONNECTED) {
            snapshot->state = NOTE4_PROVISIONING_CONNECTING;
        }
        break;
    }
}
