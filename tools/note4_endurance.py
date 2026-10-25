"""Capture allowlisted NOTE4 diagnostics, or summarize a previous capture."""
import argparse
import datetime
import json
from pathlib import Path
import re
import time

ANSI = re.compile(r"\x1b\[[0-9;]*m")
RESOURCE = re.compile(r"note4_resource: stage=([\w-]+) bytes=(\d+) internal_free=(\d+) internal_min=(\d+) internal_largest=(\d+) psram_free=(\d+) psram_min=(\d+) psram_largest=(\d+) stack_free=(\d+)$")
SAFE = re.compile(r"(?:note4: build version=[\w.+-]+ idf=[\w.+-]+ elf=[0-9a-f]+|note4: state=\d+ slot=\d+ round=\d+|note4_screen: guest_heap=\d+ guest_limit=\d+ quota_version=\d+ cards=\d+|note4_screen: PocketJS frame presented in [14]bpp mode)$")
FAULT = re.compile(r"Guru Meditation|panic'ed|Task watchdog|Stack canary|stack overflow|abort\(\)", re.I)
FIELDS = ["bytes", "internal_free", "internal_min", "internal_largest", "psram_free", "psram_min", "psram_largest", "stack_free"]


def summarize(path):
    stages = {}
    faults = 0
    builds = []
    first = last = None
    for raw in path.read_text(encoding="utf-8").splitlines():
        record = json.loads(raw)
        timestamp = datetime.datetime.fromisoformat(record["time"])
        first = timestamp if first is None else min(first, timestamp)
        last = timestamp if last is None else max(last, timestamp)
        line = record["line"]
        faults += line == "FAULT_MARKER"
        if "note4: build " in line:
            builds.append(line)
        match = RESOURCE.search(line)
        if not match:
            continue
        values = dict(zip(FIELDS, map(int, match.groups()[1:])))
        stage = stages.setdefault(match[1], {"samples": 0, "first": values, "last": values, "minimum": values.copy(), "maximum": values.copy()})
        stage["samples"] += 1
        stage["last"] = values
        for field in FIELDS:
            stage["minimum"][field] = min(stage["minimum"][field], values[field])
            stage["maximum"][field] = max(stage["maximum"][field], values[field])
    duration = (last - first).total_seconds() if first and last else 0
    return {"observed_seconds": duration, "observed_24_hours": duration >= 86400, "fault_markers": faults,
            "boot_records": builds, "stages": stages,
            "acceptance": "Manual screen/fault timeline and steady-state memory review still required"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--hours", type=float, default=24)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--summarize", type=Path)
    args = parser.parse_args()
    if args.summarize:
        print(json.dumps(summarize(args.summarize), indent=2))
        return
    if args.output is None or args.hours <= 0:
        parser.error("Capture requires --output and positive --hours")
    # Output is exclusive; an existing capture is never overwritten.
    import serial
    with args.output.open("x", encoding="utf-8") as output:
        with serial.Serial(args.port, 115200, timeout=1, dsrdtr=False, rtscts=False) as port:
            port.dtr = False
            port.rts = False
            deadline = time.monotonic() + args.hours * 3600
            while time.monotonic() < deadline:
                line = ANSI.sub("", port.readline().decode("utf-8", errors="replace").strip())
                # Never persist arbitrary SDK/radio/HTTP output or secret-bearing lines.
                resource = RESOURCE.search(line)
                safe = SAFE.search(line)
                if FAULT.search(line):
                    selected = "FAULT_MARKER"
                elif resource:
                    selected = resource[0]
                elif safe:
                    selected = safe[0]
                else:
                    continue
                output.write(json.dumps({"time": datetime.datetime.now(datetime.timezone.utc).isoformat(), "line": selected}) + "\n")
                output.flush()
    print(json.dumps(summarize(args.output), indent=2))


if __name__ == "__main__":
    main()
