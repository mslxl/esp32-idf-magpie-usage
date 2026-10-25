# NOTE4 BLE provisioning protocol v1

Service: `7c2e0000-5b6e-4d54-9a4d-4e4f54453400`. Control write: `7c2e0001-5b6e-4d54-9a4d-4e4f54453400`. Data write: `7c2e0002-5b6e-4d54-9a4d-4e4f54453400`. Status notify: `7c2e0003-5b6e-4d54-9a4d-4e4f54453400`.

NimBLE advertises `magpie-usage-<mac suffix>` while the device is in setup. The suffix is the last three bytes of the Bluetooth MAC as six lowercase hexadecimal digits. The service UUID is in the advertising packet and the complete device name is in the scan response to fit the legacy 31-byte limit. Pairing uses display-only passkey authentication, encryption and bonding. PocketJS displays the six-digit code. GATT writes and outbound notifications require an encrypted, authenticated connection. The guest bridge receives only state/slot/round/pairing data, and exposes a bounded action request API; credentials stay in the native coordinator.

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

Maximum encoded payload is 192 bytes. The current endpoints send at most eight payload bytes per frame to fit the minimum ATT MTU of 23. A transaction is limited to 1024 bytes. Sequence zero starts or restarts a transaction; later fragments must be ordered and keep request ID, opcode, slot and total unchanged. Incomplete firmware reassembly expires after five seconds and clears on disconnect. Control carries the first fragment, Data carries the remainder.

The coordinator sends ACK/NACK after processing a complete request. Browser retries retain the same request ID. The most recent completed request is cached per connection to avoid applying a retried COMMIT twice. CRC/format errors can return ATT errors before a transaction reaches the coordinator. Frames are serialized by the client.

## Commands and responses

| Opcode | Meaning |
| --- | --- |
| 0x01 BEGIN | Copy committed configuration into RAM staging; reply STATUS |
| 0x02 GET_STATUS | Reply committed SSIDs and password-presence flags |
| 0x03 SCAN | Start an asynchronous 2.4 GHz scan |
| 0x04 SET_SLOT | Edit one staged slot |
| 0x05 COMMIT | Validate and commit the entire blob; leave setup and connect |
| 0x06 ABORT | Discard staging and leave setup |
| 0x80 ACK | One-byte status 0 |
| 0x81 NACK | One-byte error code |
| 0x82 STATUS | Current state and committed configuration metadata |
| 0x83 SCAN_RESULT | One AP record |
| 0x84 SCAN_DONE | Scan completed |
| 0x85 APPLYING | Saving configuration |
| 0x86 CONNECTING | Slot in header, round in payload |
| 0x87 CONNECTED | IP acquired |
| 0x88 CONNECTION_FAILED | Current slot failed |
| 0x89 FAILURE | Three rounds exhausted |

Error codes: 1 version, 2 length, 3 CRC, 4 sequence, 5 state, 6 validation, 7 busy, 8 unauthorized. Configuration commands require setup mode. Request IDs correlate ACK and scan/status results; unsolicited progress uses ID zero. A successful COMMIT ACK is sent after `nvs_commit`.

SET_SLOT payload: `enabled:u8, passwordMode:u8, ssidLength:u8, ssid:utf8`; mode SET additionally appends `passwordLength:u8, password:utf8`. Modes: 0 unchanged, 1 set, 2 clear. Unchanged requires the same SSID in the same committed slot. Disabled slots are empty. SSIDs are limited to 32 bytes and passwords to 64 bytes. COMMIT requires 1–5 enabled slots and rejects duplicate SSIDs. Empty password selects an open network.

STATUS payload: `state:u8, configuredMask:u8`, then five records `ssidLength:u8, ssid:utf8, passwordPresent:u8`. Password bytes are never returned. State ordinals follow `main/provisioning_state.h`: boot 0, configuring 1, discard 2, applying 3, connecting 4, connected 5, failure 6.

SCAN_RESULT payload: `ssidLength:u8, ssid:utf8, rssi:i8, channel:u8, secure:u8`. Length prefix preserves tabs and other SSID characters. Empty/hidden scan records are omitted; hidden SSIDs can be entered manually. At most 32 AP records are sent. The Wi-Fi driver owns scan results until they are consumed.

## Persistence and hardware

The native coordinator validates and stores one NVS blob with magic/version/length/generation/CRC32. Credentials are not logged, exposed to PocketJS, or sent in status notifications. The current development build does not enable flash or NVS encryption; protection against physical flash extraction requires a product key/provisioning policy.

Device UI uses PocketJS Solid, including its Modal and input lifecycle. GPIO39 is UP, GPIO0 is OK, GPIO18 is DOWN, and GPIO17 holds the battery latch. Reference: https://wiki.zectrix.com/zh/software/note4-development-guide .
