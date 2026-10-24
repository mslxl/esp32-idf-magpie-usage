"""Fetch exact upstream revisions locally; never reset an existing checkout."""

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = (
    ("pocketjs", "https://github.com/pocket-nexus/pocketjs.git",
     "e27bff3e96df3c7b3c8263a34f2362b964df04fb"),
    ("note4", "https://github.com/itopinion/zectrix-note4-epd-demo.git",
     "ca285c98ed0641f86780edb1f5ec77b0335fe649"),
)


def git(path, *args, capture=False):
    return subprocess.run(
        ["git", "-C", str(path), *args], check=True,
        stdout=subprocess.PIPE if capture else None, text=True,
    ).stdout


def head(path):
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--verify", "HEAD"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else None


def main():
    for name, url, revision in DEPENDENCIES:
        destination = ROOT / "deps" / name
        if not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            git(ROOT, "init", str(destination))
            git(destination, "remote", "add", "origin", url)

        current = head(destination)
        if current is None:
            git(destination, "fetch", "--depth=1", "origin", revision)
            git(destination, "checkout", "--detach", "FETCH_HEAD")
            current = head(destination)
        if current != revision:
            raise SystemExit(f"{destination}: wrong revision; left unchanged")
        if git(destination, "status", "--porcelain", capture=True).strip():
            raise SystemExit(f"{destination}: local changes; left unchanged")
        print(f"{name}: {revision}", flush=True)


if __name__ == "__main__":
    main()
