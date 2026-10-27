#ifndef NOTE4_BLE_PAIRING_H
#define NOTE4_BLE_PAIRING_H

#include <stdbool.h>

typedef enum {
    NOTE4_BLE_REPEAT_IGNORE = 0,
    NOTE4_BLE_REPEAT_REBOND,
} note4_ble_repeat_action_t;

/* A bonded central can ask to pair again after the two sides disagree about
 * the stored keys (for example after the device's NVS was erased). Replacing
 * the local record is allowed only for a new pairing that keeps both passkey
 * authentication and bonding; otherwise the existing bond is left untouched
 * and the request is ignored. */
note4_ble_repeat_action_t note4_ble_repeat_pairing_action(
    bool peer_found, bool new_authenticated, bool new_bonding);

/* Only an encrypted, authenticated link may carry provisioning traffic. */
bool note4_ble_link_usable(bool encrypted, bool authenticated);

#endif
