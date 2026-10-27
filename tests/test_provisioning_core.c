#include <assert.h>
#include <string.h>

#include "provisioning_state.h"
#include "wifi_config.h"

static void test_config_round_trip(void) {
    note4_wifi_config_t config;
    note4_wifi_config_clear(&config);
    assert(note4_wifi_slot_set(&config, 0, "office", "secret", true));
    assert(note4_wifi_slot_set(&config, 1, "guest", "", true));
    assert(note4_wifi_config_validate(&config));
    assert(note4_wifi_config_enabled_mask(&config) == 3U);

    note4_wifi_blob_t blob;
    assert(note4_wifi_config_serialize(&config, 7U, &blob));
    note4_wifi_config_t restored;
    assert(note4_wifi_config_deserialize(&blob, &restored));
    assert(restored.generation == 7U);
    assert(strcmp(restored.slots[0].password, "secret") == 0);
    assert(restored.slots[1].password_len == 0U);
    blob.crc32 ^= 1U;
    assert(!note4_wifi_config_deserialize(&blob, &restored));
}

static void apply(note4_provisioning_snapshot_t *state,
                  note4_provisioning_event_type_t type, uint8_t mask) {
    note4_provisioning_event_t event = {.type = type};
    note4_provisioning_apply(state, &event, mask);
}

static void test_three_rounds(void) {
    note4_provisioning_snapshot_t state;
    note4_provisioning_init(&state);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONFIG_LOADED, 0x05U);
    assert(state.state == NOTE4_PROVISIONING_CONNECTING);
    assert(state.slot == 0U);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x05U);
    assert(state.slot == 2U && state.round == 1U);
    apply(&state, NOTE4_PROVISIONING_EVENT_OK, 0x05U);
    assert(state.attempted_mask == 1U);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x05U);
    assert(state.slot == 0U && state.round == 2U);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x05U);
    assert(state.slot == 2U && state.round == 2U);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x05U);
    assert(state.slot == 0U && state.round == 3U);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x05U);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x05U);
    assert(state.state == NOTE4_PROVISIONING_FAILURE);
    apply(&state, NOTE4_PROVISIONING_EVENT_OK, 0x05U);
    assert(state.state == NOTE4_PROVISIONING_CONNECTING && state.round == 1U);
}

static void test_sparse_slots_and_disconnect(void) {
    note4_provisioning_snapshot_t state;
    note4_provisioning_init(&state);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONFIG_LOADED, 0x14U);
    assert(state.slot == 2U && state.round == 1U);
    apply(&state, NOTE4_PROVISIONING_EVENT_GOT_IP, 0x14U);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x14U);
    assert(state.state == NOTE4_PROVISIONING_CONNECTING && state.slot == 4U && state.round == 1U);
    for (unsigned i = 0; i < 6U; ++i) {
        assert(state.state == NOTE4_PROVISIONING_CONNECTING);
        apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x14U);
    }
    assert(state.state == NOTE4_PROVISIONING_FAILURE);
    apply(&state, NOTE4_PROVISIONING_EVENT_OK, 0x14U);
    assert(state.slot == 2U && state.round == 1U);
    apply(&state, NOTE4_PROVISIONING_EVENT_LONG_DOWN, 0x14U);
    apply(&state, NOTE4_PROVISIONING_EVENT_OK, 0x14U);
    assert(state.state == NOTE4_PROVISIONING_DISCARD_DIALOG);
    apply(&state, NOTE4_PROVISIONING_EVENT_CANCEL, 0x14U);
    assert(state.state == NOTE4_PROVISIONING_CONFIGURING);
    apply(&state, NOTE4_PROVISIONING_EVENT_OK, 0x14U);
    apply(&state, NOTE4_PROVISIONING_EVENT_OK, 0x14U);
    assert(state.state == NOTE4_PROVISIONING_CONNECTING && state.slot == 2U);
    note4_provisioning_init(&state);
    apply(&state, NOTE4_PROVISIONING_EVENT_NO_CONFIG, 0U);
    apply(&state, NOTE4_PROVISIONING_EVENT_OK, 0U);
    apply(&state, NOTE4_PROVISIONING_EVENT_OK, 0U);
    assert(state.state == NOTE4_PROVISIONING_BOOT);
}

static void test_automatic_retry(void) {
    assert(note4_provisioning_wifi_retry_delay_seconds(1U) == 60U);
    assert(note4_provisioning_wifi_retry_delay_seconds(2U) == 300U);
    assert(note4_provisioning_wifi_retry_delay_seconds(3U) == 900U);
    assert(note4_provisioning_wifi_retry_delay_seconds(4U) == 1800U);
    assert(note4_provisioning_wifi_retry_delay_seconds(50U) == 1800U);
    note4_provisioning_snapshot_t state;
    note4_provisioning_init(&state);
    assert(state.wifi_retry_seconds == 0U && state.storage_error == NOTE4_STORAGE_OK);
    apply(&state, NOTE4_PROVISIONING_EVENT_CONFIG_LOADED, 0x06U);
    for (unsigned i = 0; i < 6U; ++i) apply(&state, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED, 0x06U);
    assert(state.state == NOTE4_PROVISIONING_FAILURE);
    /* The backoff timer restarts a full three-round cycle from the first slot. */
    apply(&state, NOTE4_PROVISIONING_EVENT_RETRY, 0x06U);
    assert(state.state == NOTE4_PROVISIONING_CONNECTING && state.round == 1U && state.slot == 1U &&
           state.attempted_mask == 0U);
    apply(&state, NOTE4_PROVISIONING_EVENT_RETRY, 0x06U);
    assert(state.state == NOTE4_PROVISIONING_CONNECTING);
}

int main(void) {
    test_automatic_retry();
    test_sparse_slots_and_disconnect();
    test_config_round_trip();
    test_three_rounds();
    return 0;
}
