# Provisioning validation — 2026-10-24

Validation targets the black-and-white NOTE4 v1.0 on COM5 with ESP-IDF 6.1 and pinned PocketJS e27bff3e96df3c7b3c8263a34f2362b964df04fb.

## Automated results

- Host CTest: 3/3 passed (display adapter, provisioning core, BLE protocol). Includes sparse-slot ordering, three complete rounds after disconnection, retry, discard/cancel, and preserving attempted-slot progress when OK is pressed during connection.
- Browser Vitest: 9/9 passed. Includes frame CRC/encoding, password modes, complete notification reassembly and serialization of concurrent outgoing transactions.
- Browser TypeScript check and Vite production build passed.
- Chromium drove the running SolidJS page with a mocked GATT peer: read an existing SSID/password-presence flag, scan, select an SSID, enter a test password, write all five slots, COMMIT, then verify the new password input is gone and editing is disabled. No page errors.
- Chromium also verified the unsupported Web Bluetooth message. Browser screenshots were inspected. These browser runs use a simulated radio peer and do not establish real pairing or Wi-Fi connectivity.

## Device startup

The protected `just flash` workflow verified the full 16 MiB original backup and the COM5 target before flashing. The original backup was retained unchanged.

Runtime debugging found and fixed three independent startup problems:

1. NimBLE requires an access callback even for the notification-only characteristic. A NULL callback caused `ble_gatts_count_resources rc=3` and an abort.
2. The 64 KiB QuickJS screen-task stack did not fit the remaining internal RAM after Wi-Fi/BLE initialization. The screen task now uses ESP-IDF's capability-aware PSRAM stack allocation. NVS and radio operations remain on the native coordinator.
3. `vTaskDelayUntil` attempted to catch up UI turns after a multi-second EPD refresh and starved the idle task. The screen task now delays after each turn instead.

After those fixes, a 55-second boot capture recorded:

```text
NimBLE: GAP procedure initiated: advertise;
note4: state=1 slot=1 round=1
note4_screen: frame plan: regions=1 full=1 black_pixels=3790/120000
note4_screen: PocketJS frame presented in 4bpp mode
```

There were no repeated resets, aborts or watchdog reports in that capture. The final firmware, including the round-count and unsolicited status fixes, was then flashed successfully (ELF SHA-256 prefix `246f419d2`). A further 45-second boot capture again showed advertising, 3,790 dark pixels and a completed 4bpp presentation with no reset, abort or watchdog report. Serial monitoring was closed afterward, releasing COM5. This verifies rendering and presentation calls, not the panel's visible appearance.

## Magpie Usage branding — 2026-10-25

The device screen heading is `Magpie Usage`; setup instructions read `Open the setup page and connect magpie usage`. The screen uses the same MAC-derived BLE name as the GAP service and scan response. Browser headings and device-facing messages also use Magpie Usage.

The protected build/flash workflow completed with backup and flash hash verification. Startup recorded:

```text
note4_ble: advertising as magpie-usage-b86fca
note4: state=1 slot=1 round=1
note4_screen: frame plan: regions=1 full=1 black_pixels=4753/120000
note4_screen: PocketJS frame presented in 4bpp mode
```

The browser's nine tests and production build passed. Physical text appearance and scanning the new name from a real BLE central remain separate acceptance checks.

## Monochrome default — 2026-10-25

All current device pages, including the discard dialog, now default to thresholded 1bpp. Text antialiasing does not enable grayscale. The guest can opt into full-screen grayscale with `note4.display.setMode("4bpp")` and restore monochrome with `note4.display.setMode("1bpp")`; a mode change invalidates the PocketJS render target so an unchanged scene is redrawn. The presenter retains the full 1bpp rebase when returning from grayscale.

Host CTest passed 3/3. The protected build/flash workflow completed with backup and flash hash verification (ELF SHA-256 prefix `e36bfe3e0`). Startup recorded:

```text
note4_ble: advertising as magpie-usage-b86fca
note4_screen: frame plan: regions=1 full=1 black_pixels=4753/120000
note4_screen: PocketJS frame presented in 1bpp mode
```

Physical appearance of the thresholded text and dialog remains unverified. The new explicit mode-switch API has compiled but has not been exercised on the device; current pages do not invoke it.

## Network/Magpie tabs and explicit setup exit — 2026-10-25

- Host CTest: 4/4 passed, including new bounded Magpie settings, Key modes, malformed payloads, combined settings CRC, legacy Wi-Fi migration, and save-success/save-failure/explicit-exit state transitions.
- Browser Vitest: 14/14 passed, including SSID/password/endpoint/Key focus retention, tab edit preservation, separate tab submissions, continued editing after saves, exit confirmation and clearing new secrets on device exit.
- Real Chromium against the running SolidJS app with a simulated GATT peer passed network scan, one-to-five card addition, sparse saved-slot readback, continuous keyboard entry, both tab saves, repeated network save preserving Magpie metadata, browser reconnect reading endpoint/keyPresent, and explicit exit. Screenshots were inspected; no page errors occurred.
- `just build` completed using ESP-IDF 6.1 and pinned PocketJS. The application binary is `0x259c80` bytes, leaving 41% of the 4 MiB app partition free.
- This revision has not been flashed. Real BLE saving, migration/persistence across power cycles and device OK exit acceptance remain hardware checks. The tests use synthetic endpoint/Key values and do not contact a Magpie instance. Magpie quota retrieval is not implemented in this revision.

## Web settings ticket #12 — 2026-10-25 follow-up

The current implementation uses Web settings schema 2 and explicit settings encoding with CRC, revision checks and staged COMMIT. Known legacy combined settings preserve Wi-Fi and clear old gateway credentials. Unknown trailing settings extensions survive edits to known fields. Settings and display preferences are written to an inactive chunk bank before switching the manifest. Shrinking a generation reclaims its unused tail; explicit clearing removes both banks. A damaged known manifest is rejected on load but can be replaced by an explicit complete setup save; a failed replacement retains the old pointer. Unknown manifest formats are rejected.

Web validation, firmware validation and display transition rules now derive their supported timezone names from the same generator, pinned to IANA tzdata 2026.2. The Pages workflow watches the generated browser list as well as the web application.

A storage initialization failure now displays a PocketJS `STORAGE UNAVAILABLE` page and waits for OK to retry initialization before starting Wi-Fi/BLE. It preserves NVS. The product-package test verifies the displayed state; the real NVS initialization/retry path still requires fault injection on hardware.

Current automated evidence:

- `just test`: 13/13 native tests passed, including settings migration/extension retention, staged settings state, storage write/commit failures, damaged-manifest replacement, inactive-tail cleanup and explicit clear. The storage fake covers deferred and immediately visible writes/erases; this does not replace actual power-cut testing.
- Web Vitest: 20/20 tests passed; TypeScript and Vite production build passed.
- Actual pinned PocketJS package: 18/18 tests, 216 assertions, including the storage failure page, ordinary candidate materialization failure/recovery, pagination, actual font atlas metrics and time formatting.
- `bun run --cwd provisioning-web test:browser`: real Chromium with synthetic GATT passed empty/saved/sparse five-slot configuration, scan suggestions, continuous typing focus, independent Network/Magpie saves, saved HTTP/interval/timezone/layout/adaptive settings on reconnect, key-presence metadata and explicit setup exit. Screenshots were inspected. The script is checked in; screenshots remain ignored output.
- `just build`: ESP-IDF 6.1 and pinned PocketJS succeeded. Application size `0x31cba0`; `0xe3460` bytes (22%) remain in the 4 MiB application partition. The existing toolchain warning about `libc_tinystdio_ungetc.c.o` lacking `.note.GNU-stack` remains.

This follow-up has not been flashed. Ticket #12 remains open: real authenticated BLE saving, migration/reboot persistence, NVS power cuts/storage exhaustion and physical setup/exit behavior remain unverified. A simultaneous manifest-write failure and failure restoring the previous pointer reports an error but does not yet establish the ticket's old-generation preservation guarantee. The complete parent feature also still needs TLS adapter integration, atomic snapshot/render resource failures and the planned hardware endurance run. Build success does not establish physical screen correctness.

## Remaining physical acceptance

The development host did not report a present Bluetooth adapter. These cases remain unverified against a real BLE central and AP:

- Operating-system pairing prompt and the displayed six-digit code; rejection of unauthenticated writes.
- Existing configuration readback, real Wi-Fi scan and authenticated save over BLE.
- Open-network and password-protected AP connection, ordered failover after a real disconnect, and three failed rounds on the device.
- NVS persistence through a power cycle.
- Physical UP/OK/DOWN controls, discard dialog and failure retry, and visible panel appearance.

The device has not been given invented network credentials for these checks. NVS/flash encryption remains disabled in this development build; product key provisioning is not covered by this validation.
