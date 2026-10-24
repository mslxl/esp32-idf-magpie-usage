# PocketJS on ZECTRIX NOTE4: Hello World

A deliberately small proof project: PocketJS renders a static `Hello World` scene at 400×300, the firmware converts RGB565 output to the NOTE4 SSD2683 driver's 1-bpp row-major/MSB-first format, and performs one full panel refresh.

This is not a general e-paper PocketJS backend. It does not poll buttons, support partial refresh, animate, manage sleep, or initialize NOTE4 audio/I2C peripherals.

## Pinned provenance

- PocketJS: [`e27bff3e96df3c7b3c8263a34f2362b964df04fb`](https://github.com/pocket-nexus/pocketjs/tree/e27bff3e96df3c7b3c8263a34f2362b964df04fb)
- NOTE4 reference firmware and `zectrix_epd`: [`ca285c98ed0641f86780edb1f5ec77b0335fe649`](https://github.com/itopinion/zectrix-note4-epd-demo/tree/ca285c98ed0641f86780edb1f5ec77b0335fe649)

`tools/fetch_deps.py` fetches those revisions into ignored `deps/` directories, and refuses to overwrite an existing checkout at another revision or with local changes.

## Requirements

- The black-and-white ZECTRIX NOTE4 PCB v1.0 (not NOTE4C).
- ESP-IDF 6.1 installed through Espressif's normal SDK installation flow. PocketJS's components declare `>=6.0,<6.2`; IDF 5.x is unsupported by this example. The SDK checkout is intentionally external to this repository rather than vendored as a submodule.
- Python 3 to fetch dependencies.
- `pocket` CLI plus Bun (for compiling the TypeScript app), or a prebuilt `note4-hello.pocket` supplied with `-DPOCKETJS_NOTE4_PACKAGE=...`.
- The PocketJS ESP32-S3 Rust native toolchain/archives described in its ESP-IDF integration documentation. This project deliberately enables `POCKETJS_RUST_FROM_SOURCE` by default for reproducibility.

## Host conversion test

With a C compiler (or Zig):

```powershell
zig cc -std=c11 -Wall -Wextra -Werror -pedantic `
  -I main tests/test_display_adapter.c main/display_adapter.c `
  -o tests/test_display_adapter.exe
./tests/test_display_adapter.exe
```

The test covers byte count, black/white conversion, RGB565 luminance thresholding, MSB-first packing, strip offsets, row edges, and argument bounds.

## Build

From this directory, in an ESP-IDF 6.0/6.1 shell:

```powershell
python -I tools/fetch_deps.py
python -I tools/prepare_deps.py
idf.py set-target esp32s3
idf.py build
```

For an existing package:

```powershell
idf.py -DPOCKETJS_NOTE4_PACKAGE=C:/absolute/path/note4-hello.pocket build
```

## Hardware gate before flashing

Do not flash until all of these are true:

1. The connected device is confirmed to be the black-and-white NOTE4 PCB v1.0.
2. The intended serial port is identified as `COM5`.
3. A recoverable, full 16 MiB backup of the original flash exists.

After those checks, use the exact port:

```powershell
idf.py -p COM5 flash monitor
```

A successful proof is a physical full refresh displaying PocketJS-produced `Hello World`, not only a successful build or serial log.

## Display and power behavior

- `display_adapter.c` maps RGB565 to 1-bpp by an initial luminance threshold of 128; it does not dither.
- The adapter expects PocketJS's full-width RGB565 strips and preserves the SSD2683 contract: 400×300, 15,000 bytes, MSB first, `1 = white`.
- The firmware holds NOTE4 GPIO17 high before driver initialization, following the public reference board's battery-latch sequence.
- The pinned driver is prepared for IDF 6.x by exporting `esp_driver_gpio`/`esp_driver_spi`; this proof keeps the driver’s experimental 4-bpp waveform path disabled and validates the public 1-bpp refresh path.
