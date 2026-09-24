#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path

LINE = re.compile(r"TBDIAG (.*)$")
FIELD = re.compile(r"(\w+)=(\S+)")
FAULTS = ("Stack overflow", "App fault", "crashed", "HardFault", "Out of memory")


def parse(paths):
    events = []
    faults = []
    for path in paths:
        if not Path(path).exists():
            continue
        for line in Path(path).read_text(errors="replace").splitlines():
            if any(marker in line for marker in FAULTS):
                faults.append(line.strip())
            match = LINE.search(line)
            if not match:
                continue
            fields = dict(FIELD.findall(match.group(1)))
            if "event" in fields or not events:
                events.append(fields)
            else:
                events[-1].update(fields)
    return events, faults


def number(event, key):
    try:
        return int(event.get(key, ""))
    except ValueError:
        return None


def summarize(events):
    summary = {"events": len(events), "windows": {}}
    for key, pick in (("heap_min", min), ("heap_free", min), ("stack_depth", max), ("inbox_bytes", max),
                      ("outbox_bytes", max), ("timers", max), ("queued", max), ("draft_bytes", max)):
        values = [value for value in (number(event, key) for event in events) if value is not None]
        summary[key] = pick(values) if values else None
    for event in events:
        window = event.get("window", "none")
        heap = number(event, "heap_free")
        stack = number(event, "stack_depth")
        entry = summary["windows"].setdefault(window, {"heap_free": heap, "stack_depth": stack})
        if heap is not None and (entry["heap_free"] is None or heap < entry["heap_free"]):
            entry["heap_free"] = heap
        if stack is not None and (entry["stack_depth"] is None or stack > entry["stack_depth"]):
            entry["stack_depth"] = stack
    return summary


def main():
    platform, baseline_path, output = sys.argv[1:4]
    logs = sys.argv[4:]
    baseline = json.loads(Path(baseline_path).read_text())
    events, faults = parse(logs)
    summary = summarize(events)
    summary["platform"] = platform
    summary["faults"] = faults
    Path(output).write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    problems = []
    if faults:
        problems.append(f"{len(faults)} fault line(s), first: {faults[0]}")
    serial = [path for path in logs if Path(path).name == "serial.log"]
    if serial and not any(Path(path).exists() and Path(path).stat().st_size > 0 for path in serial):
        problems.append("the QEMU serial console was not captured")
    if not events:
        problems.append("no TBDIAG lines; was the PBW built with TB_DIAG=1?")
    floor = baseline["heap_floor"]
    if summary["heap_min"] is not None and summary["heap_min"] < floor:
        problems.append(f"heap_min {summary['heap_min']} B is below the floor of {floor} B")
    reference = baseline["platforms"].get(platform, {}).get("stack_depth")
    if reference and summary["stack_depth"] is not None:
        limit = int(reference * baseline["stack_growth"])
        if summary["stack_depth"] > limit:
            problems.append(f"stack_depth {summary['stack_depth']} B exceeds {limit} B ({reference} B baseline)")
    print(f"{platform}: heap_min={summary['heap_min']} stack_depth={summary['stack_depth']} events={summary['events']}")
    if problems:
        for problem in problems:
            print(f"{platform}: {problem}", file=sys.stderr)
        sys.exit(1)


main()
