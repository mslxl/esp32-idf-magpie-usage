# NOTE4 validation history

## USB navigation timing — 2026-10-26

A temporary COM5 test firmware accepted only `NAV UP` / `NAV DOWN` and submitted PRESS events through the existing coordinator button queue. Numeric timestamps measured from command recognition on the device to the end of the normal screen render/presentation path. This bypasses physical GPIO debounce and DOWN-release detection, and excludes host-to-device delivery before command recognition. Driver completion includes polling SPI transfers and waiting for display BUSY and power-off completion; it does not establish visible panel quality.

Sixteen actual page transitions succeeded across the live six-card, three-page layout: twelve individually paced transitions, then four commands sent about 120–140 ms apart. No navigation driver errors or fault markers occurred; one initial boot was recorded. Every navigation frame had `ui_ms=0` and one changed region, confirming cached layouts were used. The first live snapshot took about 99 seconds after the boot marker to be published in this run; navigation began only after publication. This startup/network delay is separate from the navigation timings and its cause was not isolated by this test.

| Measurement | Observed |
| --- | ---: |
| Eleven individually paced transitions after the first | 1109.214–1127.913 ms total, median 1125.010 ms |
| Their queue / pre-render wait | 1.522–21.469 ms |
| Their drawing / damage scan | 64–70 ms |
| Their panel/presentation stage | 1036–1048 ms |
| First transition while a preceding status frame was busy | 1610.041 ms total, including 504.560 ms queue / pre-render wait |
| Four rapid commands, each measured from its own recognition | 1140.416, 2181.161, 3181.252, 4229.274 ms |

The rapid sequence retained FIFO behavior and rendered every intermediate page. The fourth event waited 3073.610 ms before its own 1155.664 ms render. Single-page navigation is therefore about 1.12 seconds under these measured conditions, while rapid input accumulates roughly one presentation per queued event. These results do not measure physical key latency or prove the display looks correct.

Ignored allowlisted evidence: `.note4-build/validation/usb-navigation-20261026.jsonl` and `usb-navigation-20261026.summary.json`. Temporary firmware binary SHA-256 was `f9a683b6b146208c1f48c8012e8d25e725b7071cdf7c896d772556f5eb1814a9`, ELF SHA-256 `f385ff8ea7a05e76aa701f972a6cd6d9cc4642c7e3c20b7dfabf83d265c76602`. Test-only source changes were restored byte-for-byte from their pre-test copies; the diagnostic entry point is not retained in normal source. Protected `just flash` then restored the preceding normal firmware, with matching binary SHA-256 `4f9598c5a3f657152967642d0054fd943ce975b58f340a3d234e5a855cd271b8` and ELF SHA-256 `58a0e3c62012e402cf79a896c20dd7137fd6e248c956349dc6c2f3c45cc7f7a7`. Flash hash verification passed; the original 16 MiB backup hash remained unchanged. Capture and flashing closed COM5 afterward.

## BLE queue and grayscale allocation — 2026-10-26

The BLE event FIFO retains 12 entries and the same inline event format. Its 12,624-byte payload storage now explicitly allocates in PSRAM; `StaticQueue_t` remains in internal BSS. All inspected producers and the coordinator consumer use task-context queue APIs. Queue initialization allocation failure returns `ESP_ERR_NO_MEM`; NimBLE initialization failure deletes the static queue and frees its separate storage. Pairing, encrypted/authenticated writes, MITM and Secure Connections settings are unchanged.

The presenter no longer reserves a 60,000-byte grayscale frame throughout monochrome operation. It allocates the frame in PSRAM before touching the panel, converts it, calls the synchronous driver and releases it on every return path. Black-and-white full and partial presentations allocate no additional drawing buffer. Host presenter coverage includes grayscale allocation failure before panel changes and cleanup after either white-base or grayscale driver failure. CTest passed 14/14, `just build` and protected `just flash` succeeded on the previously confirmed black-and-white NOTE4 v1.0 at COM5. Network/coordinator stacks and Rust UI capacities remain unchanged.

A new 180-second allowlisted COM5 capture passed: one boot, eight TLS connections, seven complete snapshots and seven accepted publications, zero rejected publications and zero fault markers. Compared with the immediately preceding optimization run:

| Metric | First optimization | BLE queue / grayscale optimization |
| --- | ---: | ---: |
| Stable worker internal free, last sample | 103,807 B | 116,523 B |
| Snapshot-stage internal free, minimum | 103,803 B | 116,503 B |
| Historical internal free minimum | 41,148 B | 53,872 B |
| Snapshot-stage largest internal block, minimum | 31,744 B | 34,816 B |
| Historical PSRAM free minimum | 7,212,324 B | 7,260,028 B |
| Worker minimum stack headroom | 15,292 B | 15,296 B |
| Nonzero Rust layout time | 202–210 ms | 199–204 ms |
| Draw/damage time | 45–79 ms | 45–78 ms |
| Nonempty panel presentation including startup | 851–1428 ms | 840–1429 ms |

Stable internal free increased by 12,716 B (12.4 KiB), while the historical internal minimum increased by 12,724 B and stayed 21,104 B above the 32 KiB reserve. The historical PSRAM minimum improved by 47,704 B (46.6 KiB), reflecting the removed permanent gray frame minus the queue storage now in PSRAM. Remote contents and allocator effects prevent exact per-allocation attribution. Compared with the original Rust baseline, stable internal free is now 58,344 B higher (57.0 KiB).

Snapshot-to-publication delays were 1.378, 1.141, 0.303, 1.925, 1.881, 1.868 and 2.466 seconds; no overall latency improvement is claimed. Five nonempty presentations completed. Some unchanged frames report 1 ms despite zero regions; the panel range above excludes those frames. The comparison JSON's generic nonzero timing range includes them. One generic timeout label occurred, as in prior captures; the label alone does not identify a failed HTTP request.

Flashed firmware: version `1cc962b-dirty`, ESP-IDF `v6.1`, application size `0x15fe80`; binary SHA-256 `4f9598c5a3f657152967642d0054fd943ce975b58f340a3d234e5a855cd271b8`, ELF SHA-256 `58a0e3c62012e402cf79a896c20dd7137fd6e248c956349dc6c2f3c45cc7f7a7`. Runtime ELF prefix matched `58a0e3c62012e402`. Flash write verification passed. The original 16 MiB backup was rehashed after flashing and remains `765D744CB156857BD20468991498E4A5BDC26C348575F789480BD1312FD2F975`. The existing `.note.GNU-stack` linker warning remains.

Ignored evidence: `.note4-build/validation/ram-optimized2-20261026.jsonl` and `ram-optimized2-20261026-comparison.json`. No raw serial/network payloads or credentials were persisted; COM5 was released after capture. Actual authenticated BLE central traffic through the moved FIFO, physical panel appearance, buttons, larger payloads and 24-hour endurance remain unverified. No commit, push or issue modification was performed.

## Internal RAM optimization — 2026-10-26

Quota trees/strings, semantic comparison scratch, smart-order arrays, HTTP buffers and account directory/display preference/storage documents now explicitly allocate in PSRAM on ESP32. Host builds retain libc allocation and the existing failure-injection seam. Allocation checks and these model/document allocations now refer to the same external heap; there is no implicit fallback into internal RAM.

The presenter BW frame and EPD old-frame shadow now reside in PSRAM. Partial refresh converts each tightly packed rectangle into the existing BW frame scratch and synchronously submits it, eliminating the temporary internal patch and unnecessary whole-frame conversion. The driver's 1024-byte DMA scratch remains internal. Network/coordinator stacks and BLE authentication are unchanged.

Validation passed: CTest 14/14 (including injected model/document allocation failures and new presenter coverage), Rust UI 4/4, `just build`, and protected `just flash` on the previously confirmed black-and-white NOTE4 v1.0 at COM5. Presenter tests first failed on internal placement and per-refresh allocation, then passed after the change; they also cover non-byte-aligned rectangles, multiple regions, grayscale-to-BW rebasing, driver errors and construction allocation failures. An initial test expectation incorrectly assumed white unused bits; it was corrected to the adapter's existing zero-padding convention.

Both baseline and optimized captures ran for 180 seconds using existing device settings; the remote six-card contents changed slightly between runs. These are observed comparisons, not exact per-allocation attribution:

| Metric | Rust baseline | Optimized |
| --- | ---: | ---: |
| Stable worker internal free, last sample | 58,179 B | 103,807 B |
| Snapshot-stage internal free, minimum | 37,927 B | 103,803 B |
| Historical internal free minimum | 26,000 B | 41,148 B |
| Snapshot-stage largest internal block, minimum | 24,576 B | 31,744 B |
| Historical PSRAM free minimum | 7,264,048 B | 7,212,324 B |
| Accepted complete publications | 8 | 7 |
| Rejected publications / fault markers | 0 / 0 | 0 / 0 |
| Nonzero Rust layout time | 201–208 ms | 202–210 ms |
| Draw/damage time | 48–80 ms | 45–79 ms |
| Nonempty presentation time including startup | 911–1430 ms | 851–1428 ms |

Stable internal free increased by 45,628 B; the snapshot-stage minimum increased by 65,876 B. The optimized internal historical minimum stayed 8,380 B above the 32 KiB candidate reserve. The network stack retained 15,292 B minimum headroom in this run. Seven snapshot-to-publication delays were 1.365, 0.299, 0.300, 0.301, 1.878, 2.444 and 1.884 seconds; some exceed the baseline range, so this establishes memory improvement without claiming overall publication latency improvement. One generic timeout label occurred in each capture; all complete snapshots were accepted, and the label does not identify a failed HTTP request.

Optimized firmware: version `1cc962b-dirty`, ESP-IDF `v6.1`, application size `0x15fe30`; binary SHA-256 `c47c2daa025d2512e1ae848bcfe4c5db8aee7af72a3aa3038b5af176ab719f5f`, ELF SHA-256 `ef9805db51445c7d8be7333ba6dd1b5b1b66b984b77dc8ef64e4cd5affba1d72`. Flash write hashes passed. The original 16 MiB backup remained unchanged at SHA-256 `765D744CB156857BD20468991498E4A5BDC26C348575F789480BD1312FD2F975`. The existing `.note.GNU-stack` linker warning remains.

Ignored evidence: `.note4-build/validation/ram-optimized-20261026.jsonl` and `ram-optimized-20261026-comparison.json`. Capture saved only allowlisted metrics/labels and released COM5 afterward. Physical panel correctness, actual buttons, real authenticated BLE, larger payloads and 24-hour endurance remain separate acceptance checks. No commit, push or issue modification was performed.

## Native Rust UI and dependency removal — 2026-10-26

The current device UI is the fixed-capacity Rust component in `components/note4_ui`; C retains networking, BLE, settings, quota snapshots and EPD presentation. The provisioning browser now uses native TypeScript DOM controls. Device JS/JSX sources, runtime manifests, submodule checkout, package/overlay tooling, obsolete tests and generated runtime component directories were removed. Retired compiler fixture sources were also removed while retaining historical diagnostic logs. Historical entries below describe earlier firmware and do not specify the current dependencies or APIs. Their former `.pocket-build/validation/` artifact directory has moved to `.note4-build/validation/`.

Host checks passed: native CTest 13/13, Rust UI tests 4/4, provisioning browser tests 20/20, TypeScript checking and Vite production build. Real Chromium with a simulated GATT peer passed five-slot editing, sparse saved-slot readback, scan selection, continuous typing/focus, independent tab saves, secret input clearing, settings reconnect and explicit exit, with no page errors. Synthetic Rust pixel output was inspected for single/two-column pages, long-card continuation, unsupported Unicode and authentication status. These checks do not establish real BLE radio behavior or physical panel appearance.

`just build` and the protected `just flash` workflow succeeded on the previously confirmed black-and-white NOTE4 v1.0, COM5. The application is `0x15fe50` bytes, leaving 66% of the 4 MiB app partition free. The full 16 MiB original Flash backup remained unchanged; its SHA-256 is `765D744CB156857BD20468991498E4A5BDC26C348575F789480BD1312FD2F975`. Flash writes passed hash verification. The existing `.note.GNU-stack` linker warning remains.

Firmware identity: version `1cc962b-dirty`, ESP-IDF `v6.1`, ELF prefix `f221f4db5a4cf0ec`. Binary SHA-256: `6fbba2c7f1d6f6474177e47dfad4786a886c4da1cd35ab485b6f159e06383b07`; ELF SHA-256: `f221f4db5a4cf0ec86db4ac88532577aa5859b3edfaf50bb954dca6f997ec3a3`. The dirty version represents the uncommitted migration tree.

A 180-second COM5 capture using the existing saved configuration passed the repeated-publication criterion:

- One initial boot, eight complete native snapshots and eight accepted publications, each with six cards; zero rejected candidates and zero fault markers.
- Rust layout took 201–208 ms; drawing and damage comparison took 48–80 ms. Nonempty panel presentations took 911–1430 ms, including startup.
- Snapshot-to-publication delays were 1.434, 1.410, 0.545, 0.512, 0.301, 0.295, 0.302 and 0.282 seconds. This capture did not reproduce the earlier 17–76-second publication stalls. Unchanged frames require no panel refresh.
- The committed layout reported three pages and 1,829 used layout bytes; this metric excludes the reserved capacity of both fixed arenas.
- Minimum internal free memory reached 26,000 bytes, below the candidate 32 KiB reserve. Minimum free PSRAM was 7,264,048 bytes. No allocation failure was observed, but combined memory acceptance remains open.
- One generic `timeout` label was captured; it does not identify a failed HTTP transaction. Nine TLS connections and all eight complete snapshots succeeded.

The ignored allowlisted capture is `.note4-build/validation/rust-runtime-20261026.jsonl`, with a numeric summary beside it. An initial collector launch failed before opening COM5 because of an incorrect Python import path; correcting the path produced the successful capture above. No raw SDK/network logs, response bodies, account strings or credentials were persisted.

No button events occurred during the capture. Actual UP/OK/DOWN latency, physical layout/EPD appearance, real authenticated BLE and 24-hour endurance remain unverified. Short-run success does not establish that every low-memory condition is resolved. No commit, push or issue modification was performed.

## Historical provisioning validation — 2026-10-24

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

This follow-up has not been flashed. Ticket #12 remains open: real authenticated BLE saving, migration/reboot persistence, NVS power cuts/storage exhaustion and physical setup/exit behavior remain unverified. The complete parent feature also still needs TLS adapter integration, atomic snapshot/render resource failures and the planned hardware endurance run. Build success does not establish physical screen correctness.

### Storage publication recovery follow-up

Settings publication now stores a versioned, CRC-protected undo pointer before changing the active manifest. Reads honor that pointer until publication finishes. A subsequent save finishes an interrupted rollback before reusing either bank. If publication fails after the undo erase becomes visible, recovery first reinstates the undo pointer and then restores the old manifest; either successful recovery write protects the old generation.

The config-store public-interface tests cover publication-commit failure combined with failure of either recovery write. They also simulate loss of power after individual write/erase/commit operations under both deferred and immediately durable storage behavior. After reboot the settings and preferences must belong to one complete generation, and a subsequent save must succeed. These are host simulations of NVS boundaries, not physical flash interruption tests or a guarantee against arbitrary simultaneous permanent storage failures.

Review used local changes against `57277e9`, with Standards and Spec checked separately in the same session. Standards found no outstanding documented-rule violations; the NVS fake was corrected to support length queries and committed erases. Spec review caught and fixed a final-publication failure that could expose the candidate after a failed save. Hardware acceptance remains outstanding. The development host reports COM5 but no present Bluetooth adapter, so real browser pairing/save cannot be completed from this host.

After this fix, `just test` passed 13/13, Web Vitest passed 20/20, Web TypeScript/production build passed, and the packaged PocketJS tests passed 18/18 with 216 assertions. The initial firmware build was stopped by an incorrectly short command deadline; `just clean` followed by `just build` completed successfully with ESP-IDF 6.1 and pinned PocketJS. Application size is `0x31ce10`, leaving `0xe31f0` bytes (22%) in the app partition. The existing `.note.GNU-stack` toolchain warning remains. No firmware flash was performed.

## Resource and render publication follow-up — 2026-10-26

Network results now prepare a candidate smart order and retain the previous snapshot until PocketJS materialization, rendering/presentation and the coordinator's epoch check accept the candidate. Only accepted candidates update the account directory and committed smart baseline. Separate publication/result queues avoid a coordinator waiting on its own full event queue. Epoch updates and worker publication acknowledgments share a critical section. A settings change can invalidate a page after physical presentation; restoration then occurs on a later frame.

Response, model, directory and preference paths now check candidate byte limits and available free/largest blocks. Transport allocation failures have a distinct resource result, and stale errno cannot change a later TLS read/write classification. Screen/worker/coordinator diagnostics report memory and stack watermarks; startup reports build identity. `tools/magpie_fixture.py` provides a synthetic socket peer, and `tools/note4_endurance.py` captures allowlisted diagnostics without retaining arbitrary SDK/network output. These tools prepare hardware verification; they do not establish that it has occurred.

The packaged UI failure regression first failed: after a synthetic native-host setText exception, restoring old signals left later successful data unable to update the page. A Solid ErrorBoundary now reports failed materialization; settlement restores old page signals and resets the boundary so later data can render. Native status construction preserves the previous status object on failure, rejects QuickJS exception values as properties, and clears exceptions on the allocation-failure return path. Failed materialization stops before EPD presentation. This regression covers a throwable host operation, not arbitrary native allocator failure.

Automated evidence for this follow-up:

- Native public-interface CTest passed 13/13, including allocator failures, storage recovery and rejected smart-order candidates. Web Vitest passed 20/20, and Web TypeScript/Vite production build passed. Web sources did not change after those runs.
- Real Chromium with synthetic GATT passed five-slot editing, saved/sparse readback, focus retention, independent tab saves, Magpie metadata reconnect and explicit exit. Screenshots were inspected. This remains simulated radio evidence.
- The final packaged PocketJS suite passed 20/20 with 222 assertions, including rejection after presentation, UI mutation failure/rollback and subsequent successful data. It uses the actual pinned package and baked font metrics.
- The ignored `.pocket-build/validation/smoke_acceptance_tools.py` drove the fixture over a real loopback socket, checking sign-in/form/prefix/Cookie, 401, reading, 429, empty results and repeat-last behavior. Collector smoke checked allowlisting, rejection of secret-bearing lines, fault markers and a synthetic 24-hour summary. Both checks passed. No COM5 capture or real 24-hour run occurred.
- Initial firmware attempts exposed missing `esp_app_format` dependency and missing `nvs.h`; both were fixed. `just clean` then `just build` succeeded. The later build with the ErrorBoundary and atomic status fixes succeeded: application `0x31f3f0`, free `0xe0c10` (22%). The subsequent build including exception cleanup also succeeded: application `0x31f3d0`, free `0xe0c30` (22%). Its embedded version is `3a87f7f-dirty`, reflecting the pre-commit source tree; bin SHA-256 is `31fe925125f0ef16df54e8c12a9f91c831883de8ab71474969f35d50e5b78e97`, ELF SHA-256 is `215766946369184a3281c0b9cf4d4d6276437f9eb55bd5839c379bc090db0d95`. Files are `C:/n4idf-provisioning/pocketjs_note4_hello.bin` and `.elf`; the package is `.pocket-build/package/note4-hello.pocket`. The existing `.note.GNU-stack` linker warning remains.

Review covers fixed baseline `57277e995933ce3df85c49b68827a95923246a3a` through the working tree, including committed storage recovery. Per the user's instruction, both axes were reviewed in this session without agents.

### Standards

No outstanding documented-standard violations found in the reviewed changes. PocketJS components/lifecycle/input stay imported from `@pocketjs/framework/*`, while Solid primitives and ErrorBoundary come from `solid-js`. Validation output remains ignored. New resource checks follow the surrounding C error/ownership conventions. No baseline smell warrants a separate refactor in this patch.

### Spec

Three requirements remain partial:

1. **#22 recoverable allocation failure:** the pinned PocketJS Rust UI/render implementation has fatal allocation paths (`engine/ui-cabi/src/lib.rs`, and the explicit warning in `ui_qjs.h`). ErrorBoundary cannot catch native abort, and persistent OOM can also prevent rollback/reset. This is an unresolved software requirement, beyond missing hardware evidence. Preference swaps and guest status are also outside the quota rollback closure; no complete transaction guarantee is established for simultaneous failures across these paths.
2. **#22 measured combined budgets:** current limits (384 KiB body including NUL, 1536 KiB parsed model/index, 384 KiB documents, 32 KiB internal reserve, 256 KiB PSRAM reserve) are engineering candidates. Domain caps and free/largest checks do not constitute a unified allocation ledger or establish target internal/TLS/renderer peaks. Target measurements must set the final limits.
3. **#13/#22/#23 target integration and endurance:** host HTTP replay, loopback fixture and packaged JS tests do not execute the ESP TLS adapter, FreeRTOS publication handshake or physical EPD under injected failures. Real authenticated BLE, public-CA TLS, NVS space/power interruption, physical display and 24-hour combined evidence are outstanding.

Review totals: Standards 0; Spec 3 partial requirements. The principal Standards concern is none found; the principal Spec gap is unrecoverable native allocation exhaustion. No tickets were closed and parent #11 was not modified. Use `docs/note4-acceptance.md` for the 21 ordered device checks. No push, deployment or firmware flash was performed in this follow-up.

## Public HTTPS investigation — 2026-10-26

Actual COM5 captures against `ai.mslxl.com`, using only the device's existing saved configuration, exposed three successive transport problems:

1. The original 16 KiB Magpie worker stack overflowed during TLS. Baseline `d67ef56` recorded three boots and two stack-overflow markers in 18 seconds; the stack watermark at TLS start was 584 bytes. Increasing the task stack to 32 KiB stopped the observed overflow.
2. Certificate verification then failed with `No matching trusted root certificate found` and mbedTLS `-0x3000`. The full certificate bundle was present (147 roots, 67817 bytes). The public server chain includes cross-signed GTS Root R4. Enabling ESP-IDF's certificate-bundle cross-signed verification allowed TLS to connect while retaining CA, hostname and validity verification.
3. Synchronous ECC certificate verification at worker priority 5 triggered the idle-task watchdog. Running the worker at `tskIDLE_PRIORITY` allows FreeRTOS time slicing to schedule idle during this work; watchdog checks remain enabled.

The instrumented build (ELF SHA-256 prefix `91dc2be57b8634e0`) completed a 100-second capture with one initial serial-open boot, zero faults, one TLS connection, and zero quota snapshots. TLS completed approximately 5.7 seconds after `tls-start`. The initial login then returned `NOTE4_MAGPIE_AUTH_REQUIRED`; on this login path that result maps to HTTP 401. Invalid/missing login Cookies instead map to `INVALID_RESPONSE`. This demonstrates server rejection of the login request, but does not identify whether the cause is the currently stored Web key, a server key change, or an upstream authentication layer. No credentials were read back, compared, printed or captured. Temporary numeric request diagnostics were removed after establishing this boundary.

The end-to-end regression assessment deliberately remains **failed**: it requires repeated complete quota snapshots, which the current authentication rejection prevents. TLS connection alone is not authenticated quota retrieval. The worker stack watermark after TLS was 15304 bytes; minimum internal free memory was 17676 bytes, below the candidate 32 KiB reserve. No allocation failure was observed, but combined resource acceptance remains open under #22. Short fault-free captures do not establish 24-hour stability, and serial presentation does not establish physical panel appearance.

Native CTest passed 13/13 after the final source edits. The native client tests include form submission, 303/Cookie validation, separate 401/403 classification, cancellation, and 15-second no-progress / 30-second total timeouts. Host tests do not exercise the ESP32 TLS stack or scheduler; the device capture is the regression evidence for those fixes. Capture artifacts and diagnostic harnesses remain under ignored `.pocket-build/validation/`.

Standards review of this follow-up found no outstanding documented-rule violations. Spec review against #13/#22/#23 confirms certificate and scheduler corrections, while successful authenticated retrieval, the combined memory reserve, and hardware/endurance acceptance remain incomplete. No tickets were closed and parent #11 was not modified.

The cleaned firmware (ELF SHA-256 prefix `9591529408585d1e`, build version `d67ef56-dirty`) was flashed with the protected `just flash` workflow: backup SHA-256 and target identity checks passed, and all three written regions reported hash verification. The 145-second post-flash serial capture recorded one initial boot, zero fault markers, one TLS connection, and zero snapshots. It did not record a second DNS/TLS/request cycle; the authentication result is not logged by the cleaned build. Thus the live display's reported alternating titles cannot be mapped to a specific HTTP result from this capture. The end-to-end acceptance remains failed.

A no-credential public probe of `GET /`, empty-form `POST /_magpie/sign-in`, and `GET /api/usage/quotas` reached the TLS service in 1.66–2.66 seconds and each returned 401. The first response had the Magpie key-entry form; the quota request indicated no sign-in cookie. This proves DNS/TLS and route reachability, not acceptance of the device's configured key. A blank key is intentionally not evidence of the user's credential. No auth headers, cookies, keys, Wi-Fi values or response bodies were persisted.

## Authenticated quota and guest-memory investigation — 2026-10-26

Later COM5 captures using the unchanged saved settings supersede the earlier authentication-only result: sign-in returned HTTP 303 with an accepted session Cookie, followed by HTTP 200 from the quota endpoint. Both responses carried Cloudflare headers and neither carried `CF-Mitigated: challenge`. The 4,561–4,564-byte responses parsed into complete native snapshots. These observations establish successful authentication for those requests; they do not exclude other Cloudflare policies at other times.

The baseline capture (ELF prefix `cf98f29efe1d9075`) then reported two idle-task watchdog events during QuickJS string iteration and rejected the candidate: committed card count remained zero. Moving mbedTLS allocations into PSRAM (ELF prefix `9394052bf04a2a32`) raised minimum internal free memory from 17,556 to 39,988 bytes but reproduced the same display failure. TLS memory pressure therefore does not explain the entire failure.

A subsequent numeric-only diagnostic build (ELF prefix `7eb555582a8711d8`) classified the caught pagination exception as `out of memory`, then recorded `materialized=-1`, render error 257 (`ESP_ERR_NO_MEM`) and rejected publication with zero committed cards. This separates the failure from HTTP authentication and native JSON parsing. The two watchdog events precede that exception. The guest heap limit is 4 MiB; PSRAM as a whole still had free capacity. Investigation of the string iteration and guest allocations remains open. Diagnostic captures exclude response bodies, account values, credentials and Cookie contents.

Native CTest passed 13/13, and the pagination/product package checks passed 17/17 with 213 assertions before the bounded-iteration experiment. Host tests do not reproduce the target QuickJS failure. At this stage, end-to-end acceptance remained failed because the screen and coordinator rejected the snapshots.

### String iterator correction and publication recovery

A bounded target reproduction, retained in `tools/diagnostics/quickjs-string-iterator.js`, exercises four strings: ASCII, Chinese, an emoji surrogate pair, and a concatenated string. Each iterator is allowed at most UTF-16 length plus one calls. Before the correction all four failed to terminate (mask 15). Eight numeric cursor records in ELF `0ccf9565529fc53d` showed repeated `start=0 next=0 stored=0` for nonempty strings. The application’s `Array.from` therefore grew until the 4 MiB guest limit was exhausted.

The Xtensa GCC 15.2 target defines `uint32_t` as `unsigned long`, while QuickJS 0.14.0 passed that cursor’s address to `string_getc` through an `int *` cast. Those types have the same size but incompatible aliasing rules. The project-owned, source-hash-checked PocketJS overlay now uses a local `int` cursor matching `string_getc` and removes the cast. `JSString.len` has 31 bits, so this preserves its representable range. Managed dependencies and the pinned submodule remain unmodified. With this change alone, the same bounded target reproduction returned mask 0, and a real quota candidate was accepted with six cards (ELF `08c8ef03a16971b0`). One request timed out before a later successful request; seven idle watchdog events still made that capture fail acceptance.

The remaining watchdog traces were inside pagination and guest calls on the priority-4 screen task. Running that task at `tskIDLE_PRIORITY` lets idle and the existing idle-priority network worker receive CPU time while preserving higher-priority coordinator work. Watchdog enforcement remains enabled. ELF `3efa38a5be1b0b3e` passed a 180-second capture: one initial boot, mask 0, two complete snapshots, two accepted publications, zero rejections and zero faults. Both publications contained six cards. This is short-run publication recovery, not endurance or physical display acceptance.

Temporary HTTP/Cloudflare, exception-category, cursor and startup-probe instrumentation was removed, along with the application’s diagnostic iteration bound. The retained numeric `quota_publication` record reports the result after the screen/coordinator acceptance callback, and `tools/note4_endurance.py` captures and counts those records. It excludes credentials, Cookie values, response bodies and account strings. The bounded runtime reproduction remains a manual target regression aid; the current host test harness does not execute target QuickJS.

Final host checks used explicit project paths: `bun test ./tests/quota-pages.test.ts ./tests/quota-app.test.ts ./tests/quota-time.test.ts` passed 20 tests and 232 assertions; `just test` passed 13/13. An earlier `bun test tests` invocation also collected submodule tests with invalid fixture paths; it failed and reached its time limit. It is not included in the passing project result. Serial evidence does not establish physical panel appearance.

The cleaned firmware (ELF prefix `66fde4f6076d44fa`, `d67ef56-dirty`) was built and flashed through `just flash` on COM5. The original 16 MiB backup passed SHA-256 verification (`765D744CB156857BD20468991498E4A5BDC26C348575F789480BD1312FD2F975`) and was not overwritten. The 240-second capture `.pocket-build/validation/ai-mslxl-clean-publication-01.jsonl` passed the strengthened criterion: three complete native snapshots, three accepted screen/coordinator publications, six committed cards per publication, zero rejected candidates, zero fault markers and one initial boot. No diagnostic startup probe or application iteration bound was present in this build. Guest heap samples after loading quotas were 1,521,545 and 1,514,747 bytes against the unchanged 4,194,304-byte limit. Both CPU idle watchdog checks remain enabled with the five-second timeout.

Publication latency remains a performance limitation: snapshot-to-acceptance took approximately 17, 76 and 54 seconds in this capture. Minimum internal free memory reached 31,228 bytes, below the candidate 32 KiB reserve, so combined resource acceptance remains open. The collector's one generic `timeout` label is not sufficient to identify a failed HTTP request. Short-run quota publication now passes; 24-hour endurance, input responsiveness during pagination and physical panel appearance remain unverified. Final review found no outstanding repository-rule violations in this correction; the resource, performance and hardware acceptance limits above remain explicit. No commit, push or issue modification was performed.

## Remaining physical acceptance

The development host did not report a present Bluetooth adapter. These cases remain unverified against a real BLE central and AP:

- Operating-system pairing prompt and the displayed six-digit code; rejection of unauthenticated writes.
- Existing configuration readback, real Wi-Fi scan and authenticated save over BLE.
- Open-network and password-protected AP connection, ordered failover after a real disconnect, and three failed rounds on the device.
- NVS persistence through a power cycle.
- Physical UP/OK/DOWN controls, discard dialog and failure retry, and visible panel appearance.
- Automatic Wi-Fi retry from FAILURE (1, 5, 15 and 30 minute backoff with the radio stopped), immediate retry on OK, and backoff reset on IP, OK, long DOWN and COMMIT.
- Storage erase recovery: a `note4_cfg` partition reporting no free pages or a newer NVS version shows the needs-erase storage error; long DOWN erases only that partition and boot continues into setup.
- BLE lifecycle: NimBLE starts on setup entry and stops after leaving setup, with the final ACK delivered first; MTU-sized notifications with a large MTU and eight-byte fragments with MTU 23.
- Power management: DFS and light sleep in release builds (light sleep disabled in the development screenshot build), GPIO wake for buttons and charge pins, and current draw in idle, setup and failure states.

The device has not been given invented network credentials for these checks. NVS/flash encryption remains disabled in this development build; product key provisioning is not covered by this validation.
