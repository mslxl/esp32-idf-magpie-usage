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

## Remaining physical acceptance

The development host did not report a present Bluetooth adapter. These cases remain unverified against a real BLE central and AP:

- Operating-system pairing prompt and the displayed six-digit code; rejection of unauthenticated writes.
- Existing configuration readback, real Wi-Fi scan and authenticated save over BLE.
- Open-network and password-protected AP connection, ordered failover after a real disconnect, and three failed rounds on the device.
- NVS persistence through a power cycle.
- Physical UP/OK/DOWN controls, discard dialog and failure retry, and visible panel appearance.

The device has not been given invented network credentials for these checks. NVS/flash encryption remains disabled in this development build; product key provisioning is not covered by this validation.
