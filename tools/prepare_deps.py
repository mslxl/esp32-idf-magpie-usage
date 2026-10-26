"""Validate the tracked NOTE4 display driver and waveform preparation."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EPD = ROOT / "components" / "zectrix_epd"
NOTE4_REVISION = "ca285c98ed0641f86780edb1f5ec77b0335fe649"


def main() -> None:
    production_waveform = EPD / "private_include" / "ssd2683_waveform_production.h"
    provenance_waveform = EPD / "private_include" / "ssd2683_waveform.h"
    extractor = ROOT / "tools" / "extract_waveform.py"
    if not production_waveform.exists() or not provenance_waveform.exists() or not extractor.exists():
        raise SystemExit("tracked SSD2683 waveform headers are missing")
    if "esp_driver_gpio" not in (EPD / "CMakeLists.txt").read_text():
        raise SystemExit("NOTE4 component is not prepared for ESP-IDF 6.x")
    source = (EPD / "zectrix_epd.cc").read_text()
    required = ("#include \"ssd2683_waveform_production.h\"",
                "kVendorGray16RenderWaveforms", "zectrix_epd_refresh_full_4bpp")
    if any(marker not in source for marker in required):
        raise SystemExit("tracked NOTE4 component is missing the active 4bpp path")
    start = source.index("zectrix_epd_refresh_full_4bpp")
    end = source.index("\n}", start)
    if "return ESP_ERR_NOT_SUPPORTED" in source[start:end]:
        raise SystemExit("tracked NOTE4 component still contains the disabled 4bpp stub")

    print(f"note4 component source: {NOTE4_REVISION}", flush=True)


if __name__ == "__main__":
    main()
