# NOTE4 BLE provisioning protocol v1

Service: `7c2e0000-5b6e-4d54-9a4d-4e4f54453400`. Control write: `7c2e0001-5b6e-4d54-9a4d-4e4f54453400`. Data write: `7c2e0002-5b6e-4d54-9a4d-4e4f54453400`. Status notify: `7c2e0003-5b6e-4d54-9a4d-4e4f54453400`.

NimBLE runs only while setup is visible: the host and controller start when the device enters setup and stop when it leaves (after APPLYING, once the final ACK and state notification are queued and a 300 ms grace period has passed). Light sleep is held off while BLE runs. NimBLE advertises `magpie-usage-<mac suffix>` while the device is in setup. The suffix is the last three bytes of the Bluetooth MAC as six lowercase hexadecimal digits. The service UUID is in the advertising packet and the complete device name is in the scan response to fit the legacy 31-byte limit. Pairing uses display-only passkey authentication, encryption and bonding; encryption and identity key-distribution masks are enabled in both directions so a bonded central's resolvable address can be matched to its stored bond. The Rust UI displays the six-digit code. GATT writes and outbound notifications require an encrypted, authenticated connection. The screen receives only display-safe state and actions; credentials stay in the native coordinator.

Bonding records are persisted, so a disconnect, leaving and re-entering setup, or a reboot reuses the existing pairing without another six-digit prompt. If a bonded central starts a new pairing because the stored keys differ, the device keeps the same security requirements: it deletes its stale local record and continues only when the central still requests passkey authentication and bonding, so the screen shows a new six-digit code. A request that would downgrade authentication or skip bonding is ignored and the link is not accepted.

## Frames

All multibyte fields are little endian. A frame has ten header bytes, payload, and two CRC bytes:

| Offset | Field |
| --- | --- |
| 0 | Protocol version, 1 |
| 1 | Opcode |
| 2–3 | Request ID, nonzero for requests |
| 4 | Slot, 0–4 for SET_SLOT |
| 5 | Sequence, zero based |
| 6 | Total sequences |
| 7–8 | Payload length |
| 9 | Reserved, zero |
| 10 | Payload |
| 10 + length | CRC16-IBM, initial 0xffff, polynomial 0xa001 |

Maximum encoded payload is 192 bytes. Firmware notifications carry `min(192, ATT_MTU - 3 - 12)` payload bytes per frame (eight bytes at the minimum ATT MTU of 23; the firmware prefers MTU 256). Firmware accepts writes of any payload size up to 192 bytes, so pages that write eight-byte fragments keep working. Notifications wait briefly for host buffers and fail the request if the link stays congested. A transaction is limited to 1024 bytes. Sequence zero starts or restarts a transaction; later fragments must be ordered and keep request ID, opcode, slot and total unchanged. Incomplete firmware reassembly expires after five seconds and clears on disconnect. Control carries the first fragment, Data carries the remainder.

The coordinator sends ACK/NACK after processing a complete request. Browser retries retain the same request ID. The most recent completed request is cached per connection to avoid applying a retried COMMIT twice. CRC/format errors can return ATT errors before a transaction reaches the coordinator. Frames are serialized by the client.

## Commands and responses

| Opcode | Meaning |
| --- | --- |
| 0x01 BEGIN | Copy committed configuration into RAM staging; reply STATUS |
| 0x02 GET_STATUS | Reply committed SSIDs and password-presence flags |
| 0x03 SCAN | Start an asynchronous 2.4 GHz scan |
| 0x04 SET_SLOT | Edit one staged slot |
| 0x05 COMMIT | Validate and commit the entire settings blob; stay in setup |
| 0x06 ABORT | Discard staging and leave setup (legacy command) |
| 0x07 SET_MAGPIE | Legacy gateway write: rejected with UNSUPPORTED_SCHEMA |
| 0x08 GET_MAGPIE | Legacy gateway read: rejected with UNSUPPORTED_SCHEMA |
| 0x09 EXIT_SETUP | Discard staging, leave setup and connect using saved networks |
| 0x0a GET_WEB_SETTINGS | Read committed Web/display metadata |
| 0x0b SET_WEB_SETTINGS | Edit staged schema-2 Web/display settings |
| 0x80 ACK | One-byte status 0 |
| 0x81 NACK | One-byte error code |
| 0x82 STATUS | Current state and committed configuration metadata |
| 0x83 SCAN_RESULT | One AP record |
| 0x84 SCAN_DONE | Scan completed |
| 0x85 APPLYING | Saving configuration |
| 0x86 CONNECTING | Slot in header, round in payload |
| 0x87 CONNECTED | IP acquired |
| 0x88 CONNECTION_FAILED | Current slot failed |
| 0x89 FAILURE | Three rounds exhausted; Wi-Fi is powered down and retried automatically |
| 0x8a MAGPIE_STATUS | Reserved legacy gateway response |
| 0x8b WEB_SETTINGS | Schema-2 Web/display metadata, never key bytes |

Error codes: 1 version, 2 length, 3 CRC, 4 sequence, 5 state, 6 validation, 7 busy, 8 unauthorized. Configuration commands require setup mode. Request IDs correlate ACK and scan/status results; unsolicited progress uses ID zero. A successful COMMIT ACK is sent after `nvs_commit`.

SET_SLOT payload: `enabled:u8, passwordMode:u8, ssidLength:u8, ssid:utf8`; mode SET additionally appends `passwordLength:u8, password:utf8`. Modes: 0 unchanged, 1 set, 2 clear. Unchanged requires the same SSID in the same committed slot. Disabled slots are empty. SSIDs are limited to 32 bytes and passwords to 64 bytes. Network edits require 1–5 enabled slots and reject duplicate SSIDs; a Magpie-only COMMIT may retain an empty network configuration. Empty password selects an open network.

STATUS payload: `state:u8, configuredMask:u8`, then five records `ssidLength:u8, ssid:utf8, passwordPresent:u8`. Password bytes are never returned. State ordinals follow `main/provisioning_state.h`: boot 0, configuring 1, discard 2, applying 3, connecting 4, connected 5, failure 6.

SCAN_RESULT payload: `ssidLength:u8, ssid:utf8, rssi:i8, channel:u8, secure:u8`. Length prefix preserves tabs and other SSID characters. Empty/hidden scan records are omitted; hidden SSIDs can be entered manually. At most 32 AP records are sent. The Wi-Fi driver owns scan results until they are consumed.

BEGIN payload: `schema:u8=2, expectedRevision:u32`. Empty/legacy BEGIN is rejected. Revision mismatch returns STALE_REVISION before staging changes. COMMIT increments the revision after durable saving.

SET_WEB_SETTINGS payload: `schema:u8=2, expectedRevision:u32, keyMode:u8, flags:u8, intervalSeconds:u16, timezoneLength:u8, endpointLength:u16, keyLength:u16, endpoint:utf8, key:ascii, timezone:ascii`. Flags: bit 0 allows HTTP, bit 1 enables adaptive font, bit 2 selects layout B, bit 3 selects Chinese device text (0 English, the default). Flags above 15 (bits 4–7) are rejected with VALIDATION; firmware without bit 3 rejects it the same way, so pages must not assume the language was saved unless COMMIT succeeds. Key modes: 0 keep, 1 replace, 2 clear. Address maximum is 512 bytes; replacement Web key is 16–256 bytes with characters `[A-Za-z0-9._~-]`. Keep requires an equivalent canonical instance address. Empty address requires an empty key. URLs reject userinfo, query, fragments, controls, whitespace, backslash, percent escapes and ambiguous dot/repeated-slash paths. Host case, default port and trailing slash normalize before instance comparison. HTTP is rejected unless flag 0 is set.

WEB_SETTINGS payload (flags as in SET_WEB_SETTINGS, including bit 3): `schema:u8=2, revision:u32, keyPresent:u8, flags:u8, intervalSeconds:u16, timezoneLength:u8, endpointLength:u16, endpoint:utf8, timezone:ascii`. It never contains Web key bytes. Empty timezone means not yet configured; the browser then pre-fills its own IANA zone. Supported zones are the explicit common-zone list shared by the page and firmware; unsupported values fail validation. Interval range is 30–3600 seconds. Error codes additionally include 9 UNSUPPORTED_SCHEMA, 10 STALE_REVISION, 11 NO_DIRECTORY and 12 RESOURCE_EXHAUSTED.

BEGIN copies both saved sections into staging. The browser applies only the active tab, leaving the other saved section intact. COMMIT ACK follows durable saving and returns to configuring, retaining authenticated BLE access. The user explicitly exits using EXIT_SETUP or the device OK confirmation; only then do network attempts begin. Unapplied changes are discarded on exit, disconnect or two-minute transaction expiry. Older firmware rejects the extension opcodes; the browser requires updated firmware before saving.

BEGIN_DISPLAY_PREFS also checks that the settings partition could commit the declared preference length together with the settings; otherwise it returns RESOURCE_EXHAUSTED before any upload starts.

## Wi-Fi failure and automatic retry

After three exhausted rounds the device shows FAILURE, sends 0x89 FAILURE if a page is still connected, stops the Wi-Fi driver and schedules an automatic retry. Consecutive failures wait 1, 5, 15 and then 30 minutes (30 minutes for every later failure). When the delay expires a fresh three-round cycle starts from the first enabled slot. OK on the failure screen retries immediately. The failure count resets on an acquired IP, OK, long DOWN (entering setup) and a successful COMMIT. The screen shows the remaining wait.

## Persistence and hardware

The native coordinator stores an explicitly encoded `N4S2` Wi-Fi/Web settings record with length, revision and CRC32 under `web_settings_v2`. Known legacy combined records are validated by version/size/CRC, then Wi-Fi is retained while old gateway address/key are cleared. Wi-Fi-only records also migrate. Invalid or unknown records produce an explicit error and never fall back to stale credentials. Settings live in the dedicated `note4_cfg` NVS partition. Initialization never erases it. If initialization fails, the screen shows a storage error and OK retries. When NVS reports that the partition needs an erase (no free pages or a newer format), holding DOWN erases the settings partition and boot continues into setup; this deletes saved Wi-Fi and Web settings. Saving uses NVS blob replacement and commit; no old record is erased to make room. Credentials are not logged, exposed to the screen, or sent in status notifications. The current development build does not enable flash or NVS encryption; protection against physical flash extraction requires a product key/provisioning policy.

NimBLE stores BLE bonds in the default `nvs` partition under its own `nimble_bond` namespace, separately from `note4_cfg`. `CONFIG_BT_NIMBLE_NVS_PERSIST` is enabled, and `ble_store_config_init()` restores the saved peer and local security records whenever the stack starts. Stopping the stack on setup exit therefore does not discard the bond, and a later setup session or cold boot reuses it. The settings-partition erase described above leaves these records intact. The default-NVS erase path used when that partition itself needs recovery, a full flash erase, or reflashing with an erased NVS removes them. If the host keeps a bond the device no longer has and the operating system does not start a new pairing, remove `magpie-usage-xxxxxx` from the host's Bluetooth settings once; the next successful pairing is then retained on both sides. Because NVS encryption is not enabled in this development build, the persisted bond keys are also exposed to a physical flash dump; enabling flash/NVS encryption is required to protect them.

Device UI uses standalone Rust layout and rasterization, including setup and discard screens. GPIO39 is UP, GPIO0 is OK, GPIO18 is DOWN, and GPIO17 holds the battery latch. Reference: https://wiki.zectrix.com/zh/software/note4-development-guide .
