"""Capture a checksum-verified current NOTE4 framebuffer from development firmware."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import time
import zlib

HEADER = re.compile(rb"N4SHOT BEGIN 400 300 15000 (\d+) (\d+) ([0-9a-f]{8})")
ROW = re.compile(rb"N4SHOT ROW (\d{3}) ([0-9a-f]{100})")
END = re.compile(rb"N4SHOT END ([0-9a-f]{8})")


class Frame:
    def __init__(self):
        self.metadata = None
        self.rows = {}
        self.started = 0.0

    def feed(self, line):
        match = HEADER.search(line)
        if match:
            page, pages = int(match[1]), int(match[2])
            self.metadata = None
            self.rows = {}
            if not 0 <= page < pages <= 65536:
                return None
            self.metadata = {"width": 400, "height": 300, "page": page + 1,
                             "pages": pages, "fnv1a": match[3].decode()}
            self.started = time.monotonic()
            return None
        if self.metadata is None:
            return None
        match = ROW.search(line)
        if match:
            number = int(match[1])
            if number >= 300 or number in self.rows:
                self.metadata = None
            else:
                self.rows[number] = bytes.fromhex(match[2].decode())
            return None
        match = END.search(line)
        if not match:
            return None
        metadata = self.metadata
        self.metadata = None
        if len(self.rows) != 300:
            return None
        pixels = b"".join(self.rows[y] for y in range(300))
        checksum = 2166136261
        for byte in pixels:
            checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
        if f"{checksum:08x}" != metadata["fnv1a"] or match[1].decode() != metadata["fnv1a"]:
            return None
        return pixels, metadata


def png_bytes(pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    scanlines = b"".join(b"\0" + bytes(byte ^ 255 for byte in pixels[y * 50:(y + 1) * 50]) for y in range(300))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 400, 300, 1, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))


def capture(port_name, seconds):
    import serial

    frame = Frame()
    started = time.monotonic()
    last_request = -10.0
    # Set control lines before opening, so screenshots do not reset the device.
    with serial.Serial() as port:
        port.port = port_name
        port.baudrate = 115200
        port.timeout = 0.25
        port.write_timeout = 0.25
        port.dsrdtr = False
        port.rtscts = False
        port.dtr = False
        port.rts = False
        port.open()
        while time.monotonic() - started < seconds:
            now = time.monotonic()
            if frame.metadata is not None and now - frame.started > 15:
                frame.metadata = None
            if frame.metadata is None and now - started - last_request >= 3:
                try:
                    port.write(b"SHOT\n")
                except serial.SerialTimeoutException as error:
                    raise ValueError("Screenshot command write timed out. Flash development firmware with "
                                     "'just flash-dev' and close other serial monitors.") from error
                last_request = now - started
            result = frame.feed(port.read_until(b"\n", 1024))
            if result is not None:
                return result
    raise ValueError("No complete framebuffer received. Flash development firmware with 'just flash-dev'; "
                     "wait for a connected quota page and close other serial monitors.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--seconds", type=float, default=120)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    output = args.output or Path(".note4-build/validation/screenshots") / (
        datetime.datetime.now().strftime("note4-%Y%m%d-%H%M%S-%f") + ".png")
    if output.suffix.lower() != ".png":
        parser.error("--output must end in .png")
    sidecar = output.with_suffix(".json")
    if output.exists() or sidecar.exists():
        parser.error("Refusing to overwrite an existing screenshot or metadata file")
    try:
        pixels, metadata = capture(args.port, args.seconds)
        png = png_bytes(pixels)
        metadata.update(png_sha256=hashlib.sha256(png).hexdigest(),
                        black_pixels=sum(byte.bit_count() for byte in pixels))
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("xb") as file:
            file.write(png)
        with sidecar.open("x", encoding="utf-8") as file:
            json.dump(metadata, file, indent=2)
        print(json.dumps(metadata, indent=2))
        print(f"Saved {output}")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Screenshot failed: {error}\n")


if __name__ == "__main__":
    main()
