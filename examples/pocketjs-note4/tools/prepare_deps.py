"""Apply the narrow IDF 6.x compatibility patch to the pinned NOTE4 driver."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NOTE4 = ROOT / "deps" / "note4" / "components" / "zectrix_epd"


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"{path}: expected compatibility patch anchor is missing")
    path.write_text(text.replace(old, new, 1))


def main() -> None:
    cmake = NOTE4 / "CMakeLists.txt"
    replace_once(
        cmake,
        '    REQUIRES driver esp_timer\n',
        '    REQUIRES esp_driver_gpio esp_driver_spi esp_timer\n',
    )

    source = NOTE4 / "zectrix_epd.cc"
    replace_once(
        source,
        '#include "freertos/task.h"\n#include "ssd2683_waveform.h"\n',
        '#include "freertos/task.h"\n',
    )
    replace_once(
        source,
        '    esp_err_t InitExternalWaveform(const uint8_t* waveform, size_t size) {\n',
        '#if 0\n    esp_err_t InitExternalWaveform(const uint8_t* waveform, size_t size) {\n',
    )
    replace_once(
        source,
        '    esp_err_t RestoreOtp() {\n'
        '        internal_power_on = false;\n'
        '        esp_err_t err = HardwareReset();\n'
        '        if (err == ESP_OK) err = SelectOtp();\n'
        '        return err;\n'
        '    }\n};',
        '    esp_err_t RestoreOtp() {\n'
        '        internal_power_on = false;\n'
        '        esp_err_t err = HardwareReset();\n'
        '        if (err == ESP_OK) err = SelectOtp();\n'
        '        return err;\n'
        '    }\n#endif\n};',
    )
    replace_once(
        source,
        'extern "C" esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t handle,\n'
        '                                                    const uint8_t* framebuffer,\n'
        '                                                    size_t framebuffer_size) {\n'
        '    if (handle == nullptr || framebuffer == nullptr ||\n'
        '        framebuffer_size != ZECTRIX_EPD_4BPP_FRAME_BYTES) {\n'
        '        return ESP_ERR_INVALID_ARG;\n'
        '    }\n'
        '    MutexGuard guard(handle->mutex);',
        'extern "C" esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t handle,\n'
        '                                                    const uint8_t* framebuffer,\n'
        '                                                    size_t framebuffer_size) {\n'
        '    if (handle == nullptr || framebuffer == nullptr ||\n'
        '        framebuffer_size != ZECTRIX_EPD_4BPP_FRAME_BYTES) {\n'
        '        return ESP_ERR_INVALID_ARG;\n'
        '    }\n'
        '    return ESP_ERR_NOT_SUPPORTED;\n#if 0\n'
        '    MutexGuard guard(handle->mutex);',
    )
    replace_once(
        source,
        '    handle->shadow_valid = false;\n    return err;\n}\n',
        '    handle->shadow_valid = false;\n    return err;\n#endif\n}\n',
    )


if __name__ == "__main__":
    main()
