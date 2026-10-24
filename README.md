# PocketJS NOTE4 Product Firmware

This repository contains the NOTE4 product firmware. It renders a PocketJS `Hello World` scene at 400×300 and presents it on the NOTE4 SSD2683 panel. The display path keeps a persistent RGB565 scene, converts ordinary UI damage to 1bpp, and converts selected grayscale UI regions to 4bpp.

The first supported refresh policy is **1bpp partial refresh plus 4bpp full-screen refresh**. A 4bpp update always covers the full panel. Returning from 4bpp to 1bpp performs a full 1bpp rebase before partial updates resume. 4bpp partial waveform support is not enabled.

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

For direct ESP-IDF commands, prepare the pinned package first and use the same explicit build directory:

```powershell
just package-pinned
idf.py --project-dir . -B .pocket-build/idf `
  -DPOCKETJS_NOTE4_PACKAGE=".pocket-build/package/note4-hello.pocket" `
  set-target esp32s3
idf.py --project-dir . -B .pocket-build/idf `
  -DPOCKETJS_NOTE4_PACKAGE=".pocket-build/package/note4-hello.pocket" build
```

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

- `display_adapter.c` maps RGB565 to 1bpp with an initial luminance threshold of 128 and maps grayscale regions to 4bpp levels 0..15. It does not dither.
- 1bpp output is 400×300, row-major, MSB-first, and 15,000 bytes. The SSD2683 driver maintains an old-image shadow for transition-based partial refresh.
- 4bpp output is 400×300, two pixels per byte, high nibble first, and 60,000 bytes. Non-grayscale pixels in a 4bpp frame use only black or white levels.
- A damage update that intersects a grayscale UI region uses one full 4bpp refresh. Other damage uses 1bpp partial refresh after a valid full 1bpp baseline.
- After a 4bpp refresh, the driver invalidates the 1bpp shadow. The next 1bpp presentation performs a full rebase before partial refreshes.
- The firmware holds NOTE4 GPIO17 high before driver initialization, following the public reference board's battery-latch sequence.
- The full 4bpp path uses the pinned production payload and its external-waveform pass sequence. The panel's visible gray states and refresh timing remain temperature- and waveform-dependent.
