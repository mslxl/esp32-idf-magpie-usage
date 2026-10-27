#include "ble_pairing.h"

note4_ble_repeat_action_t note4_ble_repeat_pairing_action(
    bool peer_found, bool new_authenticated, bool new_bonding) {
    return peer_found && new_authenticated && new_bonding
        ? NOTE4_BLE_REPEAT_REBOND
        : NOTE4_BLE_REPEAT_IGNORE;
}

bool note4_ble_link_usable(bool encrypted, bool authenticated) {
    return encrypted && authenticated;
}
