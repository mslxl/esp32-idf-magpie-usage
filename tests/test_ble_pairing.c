#include <assert.h>

#include "ble_pairing.h"

int main(void) {
    /* A stale bond may be replaced only when the peer still asks for a
     * passkey-authenticated, bonded pairing. */
    assert(note4_ble_repeat_pairing_action(true, true, true) == NOTE4_BLE_REPEAT_REBOND);
    assert(note4_ble_repeat_pairing_action(true, false, true) == NOTE4_BLE_REPEAT_IGNORE);
    assert(note4_ble_repeat_pairing_action(true, true, false) == NOTE4_BLE_REPEAT_IGNORE);
    assert(note4_ble_repeat_pairing_action(false, true, true) == NOTE4_BLE_REPEAT_IGNORE);

    /* Provisioning is never available on an unencrypted or unauthenticated
     * link, including one that is only encrypted. */
    assert(!note4_ble_link_usable(false, false));
    assert(!note4_ble_link_usable(true, false));
    assert(!note4_ble_link_usable(false, true));
    assert(note4_ble_link_usable(true, true));
    return 0;
}
