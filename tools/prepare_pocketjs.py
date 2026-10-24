"""Prepare the pinned PocketJS checkout for local TypeScript compilation."""

from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT
POCKETJS = REPOSITORY / "third_party" / "pocketjs"
FRAMEWORK_LINK = ROOT / "node_modules" / "@pocketjs" / "framework"
SOLID_LINK = POCKETJS / "framework" / "node_modules" / "solid-js"
POCKETJS_REVISION = "e27bff3e96df3c7b3c8263a34f2362b964df04fb"
PREPARER = POCKETJS / "hosts" / "esp-idf" / "components" / "pocketjs_guest" / "tools" / "prepare_quickjs.py"
EXPECTED_HASH = "8779a5050c2a78905b9f5fa671f8e33920d4ec9d82cc99157cf12bd5339e4a48"
QUICKJS_014_HASH = "36128da188cb236ffd029dd3c672ff8f85e5a196a9211e267a515c8efc1ab52c"
OVERLAY_ROOT = ROOT / ".pocket-build" / "pocketjs-overlay"
OVERLAY_COMPONENT = OVERLAY_ROOT / "pocketjs_guest"
OVERLAY_PREPARER = OVERLAY_COMPONENT / "tools" / "prepare_quickjs.py"


def ensure_junction(link: Path, target: Path) -> None:
    link.parent.mkdir(parents=True, exist_ok=True)
    if link.exists() or link.is_symlink():
        if link.resolve() == target.resolve():
            return
        raise SystemExit(f"refusing to replace existing path: {link}")
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], check=True)
    else:
        link.symlink_to(target, target_is_directory=True)


def main() -> None:
    if not (POCKETJS / "package.json").exists():
        raise SystemExit("PocketJS submodule is missing; run git submodule update --init --recursive")
    current = subprocess.run(
        ["git", "-C", str(POCKETJS), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if current != POCKETJS_REVISION:
        raise SystemExit(f"PocketJS is at {current}, expected {POCKETJS_REVISION}")
    if not (POCKETJS / "node_modules" / "solid-js").exists():
        raise SystemExit("PocketJS JavaScript dependencies are missing; run bun install in third_party/pocketjs")

    ensure_junction(FRAMEWORK_LINK, POCKETJS)
    ensure_junction(SOLID_LINK, POCKETJS / "node_modules" / "solid-js")

    if not PREPARER.exists():
        raise SystemExit("PocketJS QuickJS preparer is missing")
    text = PREPARER.read_text()
    if QUICKJS_014_HASH in text:
        patched = text
    elif EXPECTED_HASH in text:
        patched = text.replace(EXPECTED_HASH, QUICKJS_014_HASH, 1)
    else:
        raise SystemExit("unexpected PocketJS preparer; review the pinned dependency before patching")
    if OVERLAY_ROOT.exists():
        shutil.rmtree(OVERLAY_ROOT)
    overlay_preparer = OVERLAY_PREPARER
    overlay_preparer.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(POCKETJS / "hosts" / "esp-idf" / "components" / "pocketjs_guest",
                    OVERLAY_COMPONENT, dirs_exist_ok=True)
    overlay_preparer.write_text(patched, encoding="utf-8", newline="\n")
    overlay_cmake = overlay_preparer.parent.parent / "CMakeLists.txt"
    cmake_text = overlay_cmake.read_text(encoding="utf-8")
    cmake_text = cmake_text.replace(
        'set(qjs_preparer "${CMAKE_CURRENT_LIST_DIR}/tools/prepare_quickjs.py")',
        f'set(qjs_preparer "{overlay_preparer.as_posix()}")',
    )
    overlay_cmake.write_text(cmake_text, encoding="utf-8", newline="\n")
    print(f"PocketJS QuickJS overlay prepared: {overlay_preparer}", flush=True)
    print(f"PocketJS prepared: {POCKETJS_REVISION}", flush=True)


if __name__ == "__main__":
    main()
