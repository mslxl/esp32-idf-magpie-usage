"""Extract the pinned NOTE4 gray16 render payload into a firmware header."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components" / "zectrix_epd" / "private_include" / "ssd2683_waveform.h"
OUTPUT = ROOT / "components" / "zectrix_epd" / "private_include" / "ssd2683_waveform_production.h"
WAVEFORM_SIZE = 535
PASS_COUNT = 5
LEVEL_COUNT = 16
EXPECTED_PAYLOAD_HASHES = (0x7ABDAE2D, 0x87F8AEC2, 0x51B56FB5, 0x1D5936AF, 0x3078F509)


def parse_hex_array(text: str, name: str, expected: int) -> list[int]:
    match = re.search(
        rf"{re.escape(name)}\s*=\s*\{{\{{(?P<body>.*?)\}}\}};",
        text,
        re.DOTALL,
    )
    if match is None:
        raise SystemExit(f"missing {name} in pinned waveform header")
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)", match.group("body"))]
    if len(values) != expected:
        raise SystemExit(f"{name} has {len(values)} values, expected {expected}")
    if any(value > 0xFF for value in values):
        raise SystemExit(f"{name} contains a value outside one byte")
    return values


def parse_targets(text: str) -> list[tuple[int, int, int]]:
    match = re.search(
        r"kVendorGray16Targets\s*=\s*\{\{(?P<body>.*?)\}\};",
        text,
        re.DOTALL,
    )
    if match is None:
        raise SystemExit("missing kVendorGray16Targets in pinned waveform header")
    targets = [
        tuple(int(value, 16) for value in values)
        for values in re.findall(
            r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*0x([0-9A-Fa-f]+)\s*\}",
            match.group("body"),
        )
    ]
    if len(targets) != LEVEL_COUNT:
        raise SystemExit(f"gray16 target table has {len(targets)} entries, expected {LEVEL_COUNT}")
    if any(base < 1 or base > 5 or alternate < 1 or alternate > 5 or mask > 0xFF
           for base, alternate, mask in targets):
        raise SystemExit("gray16 target table contains an invalid vendor table or mask")
    return targets


def fnv1a32(values: list[int]) -> int:
    result = 0x811C9DC5
    for value in values:
        result ^= value
        result = (result * 0x01000193) & 0xFFFFFFFF
    return result


def make_waveform(vendor: list[int], targets: list[tuple[int, int, int]], pass_index: int) -> list[int]:
    waveform = vendor.copy()
    codes = [(5, 5, 0)] * 4
    for slot in range(3):
        level = pass_index * 3 + slot
        if level < 15:
            codes[slot + 1] = targets[level]

    for code, (base_table, alternate_table, mask) in enumerate(codes):
        destination_begin = 7 + (code + 1) * 88
        for record in range(8):
            source_table = alternate_table if mask & (1 << record) else base_table
            source = 7 + source_table * 88 + record * 11
            destination = destination_begin + record * 11
            waveform[destination:destination + 11] = vendor[source:source + 11]
    return waveform


def format_values(values: list[int]) -> str:
    lines = []
    for offset in range(0, len(values), 16):
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in values[offset:offset + 16]) + ",")
    return "\n".join(lines)


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    vendor = parse_hex_array(text, "kVendorGray4Waveform", WAVEFORM_SIZE)
    targets = parse_targets(text)
    waveforms = [make_waveform(vendor, targets, pass_index) for pass_index in range(PASS_COUNT)]
    if any(len(waveform) != WAVEFORM_SIZE for waveform in waveforms):
        raise SystemExit("generated waveform has an unexpected size")
    if any(waveform[:7] != vendor[:7] for waveform in waveforms):
        raise SystemExit("generated waveform changed the vendor analog configuration")
    for waveform in waveforms:
        for record in range(8):
            offset = 7 + 88 + record * 11
            if waveform[offset + 1] != 0xFF or waveform[offset + 2] != 0xFF:
                raise SystemExit("generated waveform drives hold/code-0 table")

    payload_hashes = [fnv1a32(waveform) for waveform in waveforms]
    if tuple(payload_hashes) != EXPECTED_PAYLOAD_HASHES:
        raise SystemExit("pinned waveform payload changed; review the source revision")
    output = f'''#ifndef SSD2683_WAVEFORM_PRODUCTION_H_
#define SSD2683_WAVEFORM_PRODUCTION_H_

#include <array>
#include <cstddef>
#include <cstdint>

namespace ssd2683_waveform {{

// Generated from the pinned NOTE4 source revision by extract_waveform.py.
// Source revision: ca285c98ed0641f86780edb1f5ec77b0335fe649
// The experimental generator remains available for provenance, but firmware
// includes this validated payload so its source-only assertions do not run.
constexpr size_t kWaveformSize = {WAVEFORM_SIZE};
constexpr size_t kVendorGray16RenderPassCount = {PASS_COUNT};
constexpr size_t kVendorGray16LevelCount = {LEVEL_COUNT};
constexpr size_t kVendorGray16RenderWaveformHashCount = {PASS_COUNT};

constexpr std::array<std::array<uint8_t, kWaveformSize>,
                     kVendorGray16RenderPassCount>
kVendorGray16RenderWaveforms = {{{{
'''
    for waveform in waveforms:
        output += "    {{\n" + format_values(waveform) + "\n    }},\n"
    output += "}};\n\n"
    output += "constexpr std::array<uint32_t, kVendorGray16RenderWaveformHashCount>\n"
    output += "    kVendorGray16RenderWaveformHashes = {{\n        "
    output += ", ".join(f"0x{value:08X}u" for value in payload_hashes)
    output += "\n    }};\n\n"
    output += '''constexpr size_t VendorGray16RenderPassOfLevel(size_t level) {
    return level < 15 ? level / 3 : kVendorGray16RenderPassCount;
}

constexpr uint8_t VendorGray16RenderCodeOfLevel(size_t level) {
    return level < 15 ? static_cast<uint8_t>(level % 3 + 1) : 0;
}

constexpr uint32_t Fnv1a32(const std::array<uint8_t, kWaveformSize>& waveform) {
    uint32_t result = 0x811C9DC5u;
    for (uint8_t value : waveform) {
        result ^= value;
        result *= 0x01000193u;
    }
    return result;
}

constexpr bool ValidateGray16RenderPayload() {
    for (size_t pass = 0; pass < kVendorGray16RenderPassCount; ++pass) {
        if (Fnv1a32(kVendorGray16RenderWaveforms[pass]) !=
            kVendorGray16RenderWaveformHashes[pass]) {
            return false;
        }
        for (size_t record = 0; record < 8; ++record) {
            const size_t offset = 7 + 88 + record * 11;
            if (kVendorGray16RenderWaveforms[pass][offset + 1] != 0xFF ||
                kVendorGray16RenderWaveforms[pass][offset + 2] != 0xFF) {
                return false;
            }
        }
    }
    return VendorGray16RenderPassOfLevel(15) == kVendorGray16RenderPassCount &&
           VendorGray16RenderCodeOfLevel(15) == 0;
}

static_assert(kWaveformSize == 535, "SSD2683 waveform size changed");
static_assert(kVendorGray16RenderPassCount == 5,
              "SSD2683 gray16 render pass count changed");
static_assert(kVendorGray16LevelCount == 16,
              "SSD2683 gray16 level count changed");
static_assert(ValidateGray16RenderPayload(),
              "SSD2683 production gray16 payload failed validation");

}  // namespace ssd2683_waveform

#endif  // SSD2683_WAVEFORM_PRODUCTION_H_
'''
    OUTPUT.write_text(output, encoding="utf-8", newline="\n")
    print(f"generated {OUTPUT}")
    print("payload hashes: " + ", ".join(f"0x{value:08X}" for value in payload_hashes))


if __name__ == "__main__":
    main()
