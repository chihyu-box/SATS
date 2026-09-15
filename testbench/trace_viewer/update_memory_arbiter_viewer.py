#!/usr/bin/env python3
"""
Read a trace, extract memory_arbiter access events with instruction-level
dependency information, and update the DATA variable in memory-arbiter-viewer.html.

Usage:
    python3 testbench/trace_viewer/update_memory_arbiter_viewer.py --trace <path>
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependencies

SCRIPT_DIR = Path(__file__).resolve().parent
HTML_PATH = SCRIPT_DIR / "memory-arbiter-viewer.html"

CTRLER_NAMES = ["Load", "Store", "Exec (A)", "Exec (B)", "Exec (C)"]


def parse_detail(d: str) -> dict:
    r = {}
    if not d:
        return r
    for kv in d.split(","):
        if "=" in kv:
            k, v = kv.split("=", 1)
            try:
                r[k] = int(v)
            except ValueError:
                r[k] = v
    return r


def extract(trace_path: Path) -> dict:
    arb_queued: dict[tuple, int] = {}
    arb_forwarded: dict[tuple, tuple] = {}
    accesses: list[dict] = []


    with open(trace_path) as f:
        next(f)  # skip header
        for line in f:
            parts = line.strip().split(",", 4)
            if len(parts) < 4:
                continue
            t = dependencies.parse_time(parts[0])
            engine = parts[1]
            iid = int(parts[2])
            event = parts[3]
            detail = parts[4] if len(parts) > 4 else ""

            if engine == "memory_arbiter":
                dd = parse_detail(detail)
                initiator = int(dd.get("initiator", -1))
                bank = int(dd.get("bank", -1))
                # A request is one row of one instruction from one initiator; that triple
                # links its queued, forward and complete events.
                req = (initiator, iid, int(dd.get("row", -1)))

                if event == "queued":
                    arb_queued[req] = t
                elif event == "forward":
                    qt = None
                    if req in arb_queued:
                        qt = arb_queued.pop(req)
                    arb_forwarded[req] = (t, initiator, bank, qt)
                elif event == "complete":
                    if req in arb_forwarded:
                        ft, c, b, qt = arb_forwarded.pop(req)
                        acc: dict = {"c": c, "b": b, "r": req[2], "f": ft, "e": t, "n": iid}
                        # Only a request that actually lost time counts as
                        # contended -- one that arrives in the instant its bank
                        # is released is queued and forwarded at the same
                        # timestamp and waits for nothing. MemoryArbiter's own
                        # counter draws the line in the same place.
                        if qt is not None and ft > qt:
                            acc["q"] = qt
                        accesses.append(acc)

    accesses.sort(key=lambda a: a["f"])

    max_time = max(a["e"] for a in accesses) if accesses else 0
    result: dict = {
        "accesses": accesses,
        "maxTime": max_time,
        "source": str(trace_path.name),
    }

    graph = dependencies.build(trace_path)
    result["instructions"] = {str(iid): instr for iid, instr in graph.items()}

    return result


def update_html(data: dict) -> None:
    html = HTML_PATH.read_text()
    data_json = json.dumps(data, separators=(",", ":"))
    html_new = re.sub(
        r"const DATA = \{.*?\};",
        f"const DATA = {data_json};",
        html,
        count=1,
        flags=re.DOTALL,
    )
    HTML_PATH.write_text(html_new)


def print_stats(data: dict) -> None:
    accesses = data["accesses"]
    total = len(accesses)
    contended = sum(1 for a in accesses if "q" in a)

    print(f"Extracted {total} spad accesses, maxTime={data['maxTime']} ns")

    ctrler_stats: dict[int, list[int]] = {}
    bank_stats: dict[int, list[int]] = {}
    for a in accesses:
        cs = ctrler_stats.setdefault(a["c"], [0, 0])
        cs[0] += 1
        if "q" in a:
            cs[1] += 1
        bs = bank_stats.setdefault(a["b"], [0, 0])
        bs[0] += 1
        if "q" in a:
            bs[1] += 1

    print(f"\nPer-controller:")
    for c in sorted(ctrler_stats):
        t, q = ctrler_stats[c]
        name = CTRLER_NAMES[c] if c < len(CTRLER_NAMES) else f"initiator {c}"
        pct = f"{q/t*100:.1f}%" if t else "0%"
        print(f"  {name:10s}  {t:5d} req  {q:4d} contended  ({pct})")

    print(f"\nPer-bank:")
    for b in sorted(bank_stats):
        t, q = bank_stats[b]
        pct = f"{q/t*100:.1f}%" if t else "0%"
        print(f"  Bank {b}  {t:5d} req  {q:4d} contended  ({pct})")

    print(f"\nTotal contention: {contended}/{total} ({contended/total*100:.1f}%)" if total else "")

    instrs = data.get("instructions")
    if instrs:
        print(f"\nInstructions: {len(instrs)}")
        print("  " + dependencies.summarise(instrs))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", type=Path, required=True, metavar="PATH")
    trace_path = parser.parse_args().trace
    if not trace_path.exists():
        print(f"Error: {trace_path} not found", file=sys.stderr)
        sys.exit(1)

    data = extract(trace_path)
    print_stats(data)
    update_html(data)
    print(f"\nUpdated {HTML_PATH}")


if __name__ == "__main__":
    main()
