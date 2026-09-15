#!/usr/bin/env python3
"""
Read a trace, collect what each controller records about its instructions --
the issue/dequeue/begin/end lifecycle and the per-row DRAM and SPAD events --
then update the DATA variable in controller-viewer.html.

Usage:
    python3 testbench/trace_viewer/update_controller_viewer.py --trace <path>
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependencies

SCRIPT_DIR = Path(__file__).resolve().parent
HTML_PATH = SCRIPT_DIR / "controller-viewer.html"

# Each controller: the engine that logs its lifecycle and the events that make up one
# row, in the order they are stored in the row tuple.
CONTROLLERS = {
    "load": {
        "engine": "load_controller",
        "rows": (("load_controller_dram", "dram_req"), ("load_controller_dram", "dram_resp"),
                 ("load_controller_spad", "spad_req"), ("load_controller_spad", "spad_resp")),
    },
    "store": {
        "engine": "store_controller",
        "rows": (("store_controller_spad", "spad_req"), ("store_controller_spad", "spad_resp"),
                 ("store_controller_dram", "dram_req"), ("store_controller_dram", "dram_resp")),
    },
    "gemm": {
        "engine": "exec_controller_gemm",
        "rows": (("exec_controller_gemm", "a_spad_req"), ("exec_controller_gemm", "a_spad_resp"),
                 ("exec_controller_gemm", "b_spad_req"), ("exec_controller_gemm", "b_spad_resp")),
    },
    "drain": {
        "engine": "exec_controller_drain",
        "rows": (("exec_controller_drain", "c_spad_req"), ("exec_controller_drain", "c_spad_resp")),
    },
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


def extract(trace_path: Path) -> dict:
    engine_to_ctrl = {c["engine"]: name for name, c in CONTROLLERS.items()}
    row_slot = {}  # (engine, event) -> (controller, index into the row tuple)
    for name, c in CONTROLLERS.items():
        for i, key in enumerate(c["rows"]):
            row_slot[key] = (name, i)

    instrs: dict[int, dict] = {}       # instruction id -> record
    rows: dict[tuple, list] = {}       # (id, row) -> [t0, t1, t2, t3]
    arb_queued: dict[tuple, tuple] = {}  # (initiator, instr, row) -> (t, bank)
    cfg: dict = {}                     # the config line the tracer writes first
    max_time = 0

    with open(trace_path) as f:
        next(f)
        for line in f:
            parts = line.rstrip("\n").split(",", 4)
            if len(parts) < 4:
                continue
            t = dependencies.parse_time(parts[0])
            engine, iid, event = parts[1], int(parts[2]), parts[3]
            detail = parts[4] if len(parts) > 4 else ""
            max_time = max(max_time, t)

            if engine == "config":
                cfg = parse_detail(detail)
                continue

            if engine == "top" and event == "issue":
                d = parse_detail(detail)
                op = d.pop("op")
                instrs[iid] = {"id": iid, "type": op, "issue": t, "operands": d}
                continue

            if engine in engine_to_ctrl and event in ("dequeue", "begin", "end"):
                instrs[iid][event] = t
                continue

            if engine == "memory_arbiter":
                d = parse_detail(detail)
                req = (d["initiator"], iid, d["row"])
                if event == "queued":
                    arb_queued[req] = (t, d["bank"])
                elif event == "forward" and req in arb_queued:
                    qt, bank = arb_queued.pop(req)
                    if t > qt:
                        instrs[iid].setdefault("contention", []).append({"t": qt, "d": t - qt, "bank": bank})
                continue

            slot = row_slot.get((engine, event))
            if slot:
                d = parse_detail(detail)
                n = len(CONTROLLERS[slot[0]]["rows"])
                rows.setdefault((iid, d["row"]), [None] * n)[slot[1]] = t

    for (iid, row), ts in rows.items():
        instrs[iid].setdefault("rows", []).append([row] + ts)

    graph = dependencies.build(trace_path)
    controllers = {name: {"instrs": []} for name in CONTROLLERS}
    for iid in sorted(instrs):
        rec = instrs[iid]
        ctrl = {"mvin": "load", "mvout": "store", "gemm": "gemm", "gemm_flush": "gemm", "drain": "drain"}[rec["type"]]
        if "rows" in rec:
            rec["rows"].sort()
        if graph[iid]["n"]:
            rec["name"] = graph[iid]["n"]
        controllers[ctrl]["instrs"].append(rec)

    caps = {
        "load": {"queue": cfg.get("INSTR_QUEUE_SIZE"), "buf": cfg.get("LOAD_N_DATA_BUF"), "credit": cfg.get("SPAD_CREDIT_SIZE")},
        "store": {"queue": cfg.get("INSTR_QUEUE_SIZE"), "buf": cfg.get("STORE_N_DATA_BUF"), "credit": cfg.get("SPAD_CREDIT_SIZE")},
        "gemm": {"queue": cfg.get("INSTR_QUEUE_SIZE")},
        "drain": {"queue": cfg.get("INSTR_QUEUE_SIZE")},
    }
    for name, c in controllers.items():
        c["caps"] = {k: v for k, v in caps[name].items() if v is not None}

    return {"controllers": controllers, "maxTime": max_time}


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
    print(f"Extracted maxTime={data['maxTime']} ns")
    for name, c in data["controllers"].items():
        ivs = c["instrs"]
        if not ivs:
            continue
        queued = sum(i["dequeue"] - i["issue"] for i in ivs)
        waited = sum(i["begin"] - i["dequeue"] for i in ivs)
        ran = sum(i["end"] - i["begin"] for i in ivs)
        rows = sum(len(i.get("rows", [])) for i in ivs)
        caps = " ".join(f"{k}={v}" for k, v in c["caps"].items())
        print(f"  {name:6s} {len(ivs):3d} instr  queued {queued:9.3f} ns  waiting {waited:8.3f} ns  running {ran:9.3f} ns"
              + (f"  rows {rows}" if rows else "") + (f"  [{caps}]" if caps else ""))


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
    print(f"Updated {HTML_PATH}")


if __name__ == "__main__":
    main()
