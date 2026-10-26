# NOTE4 Magpie Usage firmware

The black-and-white NOTE4 v1.0 displays Magpie account quotas on its 400×300 panel. A standalone, allocation-free Rust component handles device text, pagination, status screens and monochrome rendering. ESP-IDF handles Wi-Fi, authenticated BLE provisioning, settings, HTTPS and quota snapshots. The Web Bluetooth setup page uses TypeScript and native DOM controls.

Hold DOWN (GPIO18) for 1.2 seconds to enter setup. UP is GPIO39 and OK is GPIO0. The setup page saves up to five ordered 2.4 GHz Wi-Fi networks, the Magpie Web endpoint/key, polling interval, supported IANA timezone, A/B layout and adaptive font. Saving stays in setup; explicitly exit to connect. Existing passwords and keys never return to the browser.

See [web setup](provisioning-web/README.md), [BLE protocol](docs/ble-provisioning.md), [validation](docs/provisioning-validation.md) and [hardware acceptance](docs/note4-acceptance.md).

## Build and test

Requirements: ESP-IDF 6.1, Python 3, uv, just, Rust with the Espressif `esp` toolchain and `xtensa-esp32s3-none-elf` support. Bun is needed for the browser app. The SDK and Rust toolchain remain external to the repository.

Run all `just` commands from this repository root:

```powershell
$env:IDF_EXPORT = "C:/Users/<user>/esp/esp-idf-v6.1/export.ps1"
$env:NOTE4_BUILD_DIR = "C:/n4idf-rust"
just build
just test
just test-ui
bun install --cwd provisioning-web
bun run --cwd provisioning-web build
bun run --cwd provisioning-web test
bun run --cwd provisioning-web test:browser
```

`just prepare` generates monochrome font tables, provider icons, timezone rules and validated NOTE4 waveform tables. `just build` runs preparation before ESP-IDF. The default build directory is `.note4-build/idf`; `NOTE4_BUILD_DIR` allows a shorter Windows path. After a failed firmware build, run `just clean` before `just build`. Do not bypass preparation with a direct `idf.py build`.

The Rust renderer uses two fixed PSRAM arenas. A complete candidate layout is presented and accepted before its model becomes committed; failed candidates retain the old model. UP/DOWN navigation draws cached pages. Oversized data returns a resource error instead of allocating an unbounded UI tree. Network and BLE allocations still require resource validation on hardware.

## Flashing

Before flashing, confirm the board is the black-and-white NOTE4 PCB v1.0, the port is COM5, and the original full 16 MiB flash backup exists. The backup must never be deleted or overwritten. `device-info` identifies the chip and flash capacity; it cannot distinguish a NOTE4C panel.

```powershell
just verify-backup
just device-info
just flash
just monitor
```

`just flash` builds, verifies the backup and queries the device before writing bootloader, partition table and application. Defaults: `NOTE4_PORT=COM5` and backup `note4-build/original-flash/note4-com5-original-16MiB.bin`. Override with `NOTE4_BACKUP` and optionally `NOTE4_BACKUP_SHA256`.

For repeated USB screenshots during development, run `just flash-dev`, then `just screenshot`. Development output uses a separate `${NOTE4_BUILD_DIR}-dev` directory (override with `NOTE4_DEV_BUILD_DIR`). PNGs and checksum metadata go into `.note4-build/validation/screenshots/`. Close other serial clients before flashing or capturing. See [development screenshots](docs/development-screenshots.md).

`just build` and `just flash` always configure release mode with `NOTE4_DEVELOPMENT=OFF`; a build check rejects screenshot protocol markers or entry points in the ELF/BIN. Development capture code and its USB dependency are excluded from the release component. Run `just flash` to return a development device to release firmware.

The current UI draws black and white only. The presenter converts RGB565 into 15,000-byte MSB-first 1bpp frames, establishes a full baseline, then applies byte-aligned damage updates. The display driver retains independently tested 4bpp conversion/waveform support. GPIO17 holds the board battery latch.

The tracked display driver is derived from NOTE4 reference firmware [`ca285c98ed0641f86780edb1f5ec77b0335fe649`](https://github.com/itopinion/zectrix-note4-epd-demo/tree/ca285c98ed0641f86780edb1f5ec77b0335fe649). Production waveform extraction validates dimensions, analog prefix, hold table and hashes. Font provenance and license are in [assets/fonts](assets/fonts/README.md).

Builds, host tests and serial logs do not establish physical panel appearance. Record physical readability, refresh artifacts and button behavior separately.
