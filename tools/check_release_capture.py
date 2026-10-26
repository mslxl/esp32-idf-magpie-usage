"""Fail if NOTE4 release artifacts contain the development screenshot transport."""
import argparse
import json
from pathlib import Path


def check(build):
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    if any(Path(entry["file"]).name == "dev_capture.c" for entry in commands):
        raise ValueError("Development screenshot source is included in release compilation")
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if "NOTE4_DEVELOPMENT:BOOL=OFF" not in cache:
        raise ValueError("Release must explicitly configure NOTE4_DEVELOPMENT=OFF")
    for name in ("magpie_note4.elf", "magpie_note4.bin"):
        data = (build / name).read_bytes()
        if any(marker in data for marker in (b"N4SHOT", b"note4_capture_poll", b"note4_capture_init")):
            raise ValueError(f"Development screenshot transport found in {name}")
    print("Release verified: no screenshot source, protocol markers or entry points")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    try:
        check(args.build)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Release screenshot check failed: {error}\n")


if __name__ == "__main__":
    main()
