# NOTE4 SSD2683 component

This component is extracted from the ZECTRIX NOTE4 reference project:

- Repository: https://github.com/itopinion/zectrix-note4-epd-demo
- Source revision: `ca285c98ed0641f86780edb1f5ec77b0335fe649`
- License: see `LICENSE`

The component is kept locally because the application only needs the SSD2683
panel driver, not the complete NOTE4 reference firmware. It retains the
reference driver's full 1bpp refresh, partial 1bpp refresh, and calibrated
16-level 4bpp waveform path.

This copy uses ESP-IDF 6.x component names (`esp_driver_gpio` and
`esp_driver_spi`) in `CMakeLists.txt`. The driver requires a successful full
1bpp refresh before partial 1bpp updates. A full 4bpp refresh invalidates the
1bpp transition shadow; the application must establish a new full 1bpp base
before using partial refresh again.
