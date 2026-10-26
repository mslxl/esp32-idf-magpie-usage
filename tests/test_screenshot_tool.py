"""Host checks for screenshot framing, PNG conversion and release exclusion."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


shot = load("note4_screenshot")
release = load("check_release_capture")


class ScreenshotTests(unittest.TestCase):
    def lines(self):
        # Independent known vector: 15,000 zero bytes, FNV-1a = 12b736a5.
        return ([b"N4SHOT BEGIN 400 300 15000 1 3 12b736a5\n"]
                + [f"N4SHOT ROW {y:03d} {'00' * 50}\n".encode() for y in range(300)]
                + [b"N4SHOT END 12b736a5\n"])

    def parse(self, lines):
        frame = shot.Frame()
        result = None
        for line in lines:
            result = frame.feed(line) or result
        return result

    def test_complete_frame_ignores_unrelated_serial_lines(self):
        lines = self.lines()
        result = self.parse([b"unrelated log\n"] + lines[:100] + [b"ignored\n"] + lines[100:])
        self.assertIsNotNone(result)
        self.assertEqual(result[0], bytes(15000))
        self.assertEqual(result[1]["page"], 2)
        self.assertEqual(result[1]["pages"], 3)

    def test_missing_duplicate_corrupt_and_invalid_page_are_rejected(self):
        lines = self.lines()
        cases = [lines[:10] + lines[11:], lines[:10] + [lines[9]] + lines[10:],
                 lines[:10] + [lines[10].replace(b"00", b"ff")] + lines[11:],
                 [lines[0].replace(b"1 3", b"3 3")] + lines[1:]]
        for case in cases:
            self.assertIsNone(self.parse(case))
        frame = shot.Frame()
        for line in cases[0] + lines:
            result = frame.feed(line)
        self.assertIsNotNone(result)

    def test_png_dimensions_polarity_and_crc(self):
        pixels = b"\x80" + bytes(14999)
        png = shot.png_bytes(pixels)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        compressed = b""
        while offset < len(png):
            length = struct.unpack(">I", png[offset:offset + 4])[0]
            kind = png[offset + 4:offset + 8]
            data = png[offset + 8:offset + 8 + length]
            crc = struct.unpack(">I", png[offset + 8 + length:offset + 12 + length])[0]
            self.assertEqual(crc, zlib.crc32(kind + data))
            if kind == b"IHDR":
                self.assertEqual(struct.unpack(">IIBBBBB", data), (400, 300, 1, 0, 0, 0, 0))
            if kind == b"IDAT":
                compressed += data
            offset += length + 12
        raw = zlib.decompress(compressed)
        self.assertEqual(len(raw), 300 * 51)
        self.assertEqual(raw[:3], b"\0\x7f\xff")

    def test_release_guard_rejects_source_markers_and_cached_development_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "compile_commands.json").write_text("[]")
            (build / "CMakeCache.txt").write_text("NOTE4_DEVELOPMENT:BOOL=OFF\n")
            for name in ("magpie_note4.elf", "magpie_note4.bin"):
                (build / name).write_bytes(b"release fixture")
            release.check(build)
            (build / "magpie_note4.bin").write_bytes(b"N4SHOT BEGIN")
            with self.assertRaises(ValueError):
                release.check(build)
            (build / "magpie_note4.bin").write_bytes(b"release fixture")
            (build / "CMakeCache.txt").write_text("NOTE4_DEVELOPMENT:BOOL=ON\n")
            with self.assertRaises(ValueError):
                release.check(build)
            (build / "CMakeCache.txt").write_text("NOTE4_DEVELOPMENT:BOOL=OFF\n")
            (build / "compile_commands.json").write_text(json.dumps([{"file": "main/dev_capture.c"}]))
            with self.assertRaises(ValueError):
                release.check(build)


if __name__ == "__main__":
    unittest.main()
