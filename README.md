# PocketJS NOTE4 Product Firmware

This repository contains the NOTE4 product firmware and a SolidJS Web Bluetooth Wi-Fi setup page. The 400×300 device screens, pairing code, button actions and discard dialog use PocketJS Solid components and its RGB565 renderer. ESP-IDF NimBLE handles authenticated BLE transport; `esp_wifi` connects to up to five saved 2.4 GHz networks. Device pages default to thresholded 1bpp, including text and dialogs. A page must opt into 4bpp grayscale through `note4.display.setMode("4bpp")`; `note4.display.setMode("1bpp")` restores monochrome. All current provisioning pages use the 1bpp default.

The refresh policy is **1bpp by default, with explicit 4bpp opt-in**. Mode changes invalidate the PocketJS render target and force a full redraw. In 1bpp, the first frame establishes a full monochrome baseline and subsequent damage can use partial refresh. An opted-in 4bpp update always covers the full panel and starts from a white 1bpp baseline. Returning from 4bpp to 1bpp performs a full 1bpp rebase before partial updates resume. 4bpp partial waveform support is not enabled.

## Device settings

See [the web setup guide](provisioning-web/README.md) for pairing, scanning, ordered network slots and device controls, [the BLE protocol](docs/ble-provisioning.md) for framing and credential handling, and [the validation record](docs/provisioning-validation.md) for verified results and remaining physical acceptance. Start the page with `bun run --cwd provisioning-web dev --host 127.0.0.1`. On NOTE4 v1.0, UP is GPIO39, the front OK button is GPIO0, and the lower side DOWN button is GPIO18. Hold DOWN for 1.2 seconds to enter setup. Pairing, progress, failure/retry and discard pages run in PocketJS; the host handles radio and persistence.

The setup page has **网络** and **Magpie 配置** tabs. Applying saves the active section and stays in settings; explicitly exit through the browser or the device OK confirmation to start Wi-Fi connections. Configure the Magpie management Web address and the Web key from its `?k=` login link. HTTP requires explicit opt-in. The page also saves the reading interval (30–3600 seconds), supported IANA display timezone, A/B layout and adaptive font setting. Only credential-presence flags return to the browser. Upgrading a known legacy gateway configuration preserves Wi-Fi and clears the old Magpie credentials for reconfiguration. Settings use an explicit versioned encoding with CRC and revision checks; invalid data or NVS initialization errors never trigger a partition erase.

## Pinned provenance

- PocketJS: [`e27bff3e96df3c7b3c8263a34f2362b964df04fb`](https://github.com/pocket-nexus/pocketjs/tree/e27bff3e96df3c7b3c8263a34f2362b964df04fb)
- NOTE4 reference firmware and `zectrix_epd`: [`ca285c98ed0641f86780edb1f5ec77b0335fe649`](https://github.com/itopinion/zectrix-note4-epd-demo/tree/ca285c98ed0641f86780edb1f5ec77b0335fe649)

`third_party/pocketjs` is a Git submodule pinned to the PocketJS revision. The source tree tracks the extracted `components/zectrix_epd` module. `tools/prepare_pocketjs.py` creates the ignored JavaScript junctions and QuickJS preparation overlay required by the pinned PocketJS/quickjs-ng combination; it leaves the submodule clean.

The original NOTE4 waveform generator is retained as provenance and extraction input. Its five semantic assertions do not agree with the generated final tables under ESP-IDF 6.1, so firmware includes the generated `ssd2683_waveform_production.h` payload instead. The extractor validates the 5 × 535-byte payload dimensions, vendor analog prefix, code-0 hold table, and FNV-1a hashes.

## Requirements

- The black-and-white ZECTRIX NOTE4 PCB v1.0 (not NOTE4C).
- ESP-IDF 6.1 installed through Espressif's normal SDK installation flow. PocketJS declares `>=6.0,<6.2`; IDF 5.x is unsupported. The SDK checkout remains external to this repository.
- Python 3 and Bun for compiling the TypeScript app.
- The PocketJS ESP32-S3 Rust toolchain. Set `RUSTUP_TOOLCHAIN=esp` when building from source.

## Host conversion test

From the repository root:

```powershell
cmake -S tests -B tests/build
cmake --build tests/build --config Debug
ctest --test-dir tests/build -C Debug --output-on-failure
```

The test covers 1bpp full-frame conversion, luminance thresholding, MSB-first packing, strip offsets, row edges, 4bpp high/low nibble packing, grayscale quantization, rectangle conversion, guard bytes, and argument bounds.

## Build

Set the ESP-IDF export script and initialize the submodule from the repository root:

```powershell
$env:IDF_EXPORT = "C:/Users/<user>/esp/esp-idf-v6.1/export.ps1"
git submodule update --init --recursive
just build
```

The root `justfile` generates the pinned PocketJS package and builds ESP-IDF output in `.pocket-build/idf`. Override the build directory without editing scripts when Windows path limits require it:

```powershell
$env:NOTE4_BUILD_DIR = "C:/n4idf"
just build
```

If a build fails, run `just clean` and then `just build` with the same environment. The recipe prepares the pinned application package, UI atlas and NOTE4 waveform dependencies; running `idf.py build` alone does not replace that workflow.

The individual recipes are `just prepare`, `just test`, `just package-pinned`, `just build`, `just build-package`, `just verify-backup`, `just device-info`, `just flash`, `just monitor`, `just flash-monitor`, and `just clean`. The serial port defaults to `COM5` and can be changed with `NOTE4_PORT`. The backup path defaults to `note4-build/original-flash/note4-com5-original-16MiB.bin`; set `NOTE4_BACKUP` and optionally `NOTE4_BACKUP_SHA256` to use another verified backup.

## Hardware gate before flashing

Do not flash until all of these are true:

1. The connected device is confirmed to be the black-and-white NOTE4 PCB v1.0.
2. The intended serial port is identified as `COM5`.
3. A recoverable, full 16 MiB backup of the original flash exists.

Run the protected checks, then flash and monitor the same build directory:

```powershell
just verify-backup
just device-info
$env:NOTE4_PORT = "COM5"
just flash-monitor
```

Product validation requires the physical panel to show the PocketJS-produced scene. Build success and serial output alone do not establish display correctness. Record serial diagnostics separately from physical-panel acceptance for the supported 1bpp partial, 4bpp full-screen, and 1bpp rebase sequence.

## Display and power behavior

A future grayscale page can opt in for its lifetime, restoring the default when it unmounts:

```tsx
import { onCleanup } from "solid-js";

function GrayscalePage() {
  native.display.setMode("4bpp");
  onCleanup(() => native.display.setMode("1bpp"));
  // Return PocketJS components here.
}
```

`native` refers to the app's `globalThis.note4` bridge. The mode applies to the full screen, including overlays; gray text pixels alone never enable 4bpp.

- `display_adapter.c` maps RGB565 to 1bpp with an initial luminance threshold of 128 and maps grayscale regions to 4bpp levels 0..15. It does not dither.
- 1bpp output is 400×300, row-major, MSB-first, and 15,000 bytes. The SSD2683 driver maintains an old-image shadow for transition-based partial refresh.
- 4bpp output is 400×300, two pixels per byte, high nibble first, and 60,000 bytes. Non-grayscale pixels in a 4bpp frame use only black or white levels.
- A damage update that intersects a grayscale UI region uses one full 4bpp refresh. Other damage uses 1bpp partial refresh after a valid full 1bpp baseline.
- After a 4bpp refresh, the driver invalidates the 1bpp shadow. The next 1bpp presentation performs a full rebase before partial refreshes.
- The firmware holds NOTE4 GPIO17 high before driver initialization, following the public reference board's battery-latch sequence.
- The full 4bpp path uses the pinned production payload and its external-waveform pass sequence. The panel's visible gray states and refresh timing remain temperature- and waveform-dependent.
