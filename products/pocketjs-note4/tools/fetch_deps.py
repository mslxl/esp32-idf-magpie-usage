"""Check the PocketJS submodule required by the NOTE4 example."""

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
POCKETJS = ROOT.parent.parent / "third_party" / "pocketjs"
REVISION = "e27bff3e96df3c7b3c8263a34f2362b964df04fb"


def main() -> None:
    path = POCKETJS.resolve()
    if not (path / "hosts" / "esp-idf" / "components" / "pocketjs_guest").exists():
        raise SystemExit("PocketJS submodule is missing; run git submodule update --init --recursive")
    current = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if current != REVISION:
        raise SystemExit(f"PocketJS submodule is at {current}, expected {REVISION}")
    status = subprocess.run(
        ["git", "-C", str(path), "status", "--porcelain"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if status:
        raise SystemExit("PocketJS submodule has local changes; restore it before building")
    print(f"pocketjs: {REVISION}", flush=True)


if __name__ == "__main__":
    main()
