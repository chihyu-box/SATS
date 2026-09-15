#!/usr/bin/env python3
"""
Read a trace, extract begin/end intervals and bank contention, work out the
instruction dependency graph (see dependencies.py), then update the DATA
variable in pipeline-timeline.html.

Usage:
    python3 testbench/trace_viewer/update_pipeline_timeline.py --trace <path>
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependencies

SCRIPT_DIR = Path(__file__).resolve().parent
HTML_PATH = SCRIPT_DIR / "pipeline-timeline.html"

# The bar that carries an arbiter initiator's contention marks.
CTRLER_TO_ENGINE = {
    0: "load_controller",
    1: "store_controller",
    2: "exec_controller_gemm",
    3: "exec_controller_gemm",
    4: "exec_controller_drain",
}


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


TICK_ENGINES = {"pe_array_gemm", "pe_array_drain", "scaler_module", "skew_a", "skew_b"}


def extract(trace_path: Path) -> dict:
    intervals = []
    begins: dict[tuple, dict] = {}
    arbiter_queued: dict[tuple, tuple] = {}
    contention_events: list[tuple] = []
    ticks: dict[tuple, list] = {}


    with open(trace_path) as f:
        next(f)
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
                # A request is one row of one instruction from one initiator.
                req = (initiator, iid, int(dd.get("row", -1)))

                if event == "queued":
                    arbiter_queued[req] = (t, initiator, bank)
                elif event == "forward":
                    if req in arbiter_queued:
                        qt, ctrler, qbank = arbiter_queued.pop(req)
                        # A request forwarded in the same instant it was queued
                        # waited for nothing; MemoryArbiter does not count it
                        # either.
                        if t > qt:
                            eng = CTRLER_TO_ENGINE.get(ctrler)
                            if eng:
                                contention_events.append((eng, qt, t, qbank, iid))
                continue

            if engine in TICK_ENGINES and event == "tick":
                ticks.setdefault((engine, iid), []).append((t, detail))
                continue
            if event == "begin":
                begins[(engine, iid)] = {"time": t, "detail": detail}
            elif event == "end":
                key = (engine, iid)
                if key in begins:
                    b = begins.pop(key)
                    iv = {
                        "engine": engine,
                        "id": iid,
                        "start": b["time"],
                        "end": t,
                    }
                    pd = parse_detail(b["detail"])
                    if "buf" in pd:
                        iv["buf"] = pd["buf"]
                    intervals.append(iv)

    for (engine, iid), pts in ticks.items():
        pts.sort()
        times = [t for t, _ in pts]
        stalls = []
        for i in range(len(times) - 1):
            if times[i + 1] - times[i] > 1:
                stalls.append([times[i] + 1, times[i + 1]])
        iv = {
            "engine": engine,
            "id": iid,
            "start": times[0],
            "end": times[-1] + 1,
        }
        if stalls:
            iv["stalls"] = stalls
        for _, d in pts:
            pd = parse_detail(d)
            if "buf" in pd:
                iv["buf"] = pd["buf"]
                break
        intervals.append(iv)

    for iv in intervals:
        events = []
        for eng, qt, ft, bank, req_id in contention_events:
            if eng == iv["engine"] and req_id == iv["id"]:
                events.append({"t": qt, "d": ft - qt, "bank": bank})
        if events:
            iv["contention"] = {
                "count": len(events),
                "delayNs": sum(e["d"] for e in events),
                "events": events,
            }

    max_time = max(iv["end"] for iv in intervals) if intervals else 0
    result: dict = {"intervals": intervals, "maxTime": max_time}

    graph = dependencies.build(trace_path)
    result["instructions"] = {str(iid): instr for iid, instr in graph.items()}
    # The type comes from the instruction, not the engine: gemm_flush shares the gemm engine
    # and is the bar worth telling apart. The name is carried onto the bar for labelling.
    for iv in intervals:
        instr = graph[iv["id"]]
        iv["type"] = instr["t"]
        if instr["n"]:
            iv["name"] = instr["n"]

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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", type=Path, required=True, metavar="PATH")
    trace_path = parser.parse_args().trace
    if not trace_path.exists():
        print(f"Error: {trace_path} not found", file=sys.stderr)
        sys.exit(1)

    data = extract(trace_path)
    engines = sorted(set(iv["engine"] for iv in data["intervals"]))
    n_contend = sum(1 for iv in data["intervals"] if "contention" in iv)
    total_contend = sum(iv.get("contention", {}).get("count", 0) for iv in data["intervals"])
    print(f"Extracted {len(data['intervals'])} intervals, maxTime={data['maxTime']}")
    print(f"Engines ({len(engines)}): {', '.join(engines)}")
    if total_contend:
        print(f"Bank contention: {total_contend} events across {n_contend} instructions")

    instrs = data.get("instructions")
    if instrs:
        print("Dependencies: " + dependencies.summarise(instrs))

    update_html(data)
    print(f"Updated {HTML_PATH}")


if __name__ == "__main__":
    main()
