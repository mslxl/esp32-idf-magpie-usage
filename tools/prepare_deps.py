"""Validate the tracked NOTE4 driver and PocketJS submodule preparation."""

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
POCKETJS = ROOT / "third_party" / "pocketjs"
EPD = ROOT / "components" / "zectrix_epd"
POCKETJS_REVISION = "e27bff3e96df3c7b3c8263a34f2362b964df04fb"
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

    current = subprocess.run(
        ["git", "-C", str(POCKETJS.resolve()), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if current != POCKETJS_REVISION:
        raise SystemExit(f"PocketJS is at {current}, expected {POCKETJS_REVISION}")
    print(f"pocketjs: {POCKETJS_REVISION}", flush=True)
    print(f"note4 component source: {NOTE4_REVISION}", flush=True)


if __name__ == "__main__":
    main()
