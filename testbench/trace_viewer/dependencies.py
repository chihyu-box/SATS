#!/usr/bin/env python3
"""Instruction dependencies, in three kinds.

NECESSARY edges are carried by the addresses themselves: a read depends on the
last write it overlaps, a write on the last write plus every read since. Those
are derived here from the `top,<id>,issue` rows the simulator writes, which
name every operand of every instruction -- so the same rule applies to every
scheduler and the resulting graphs are directly comparable.

STRUCTURAL edges come from the machine having one of something. The PE array
runs one gemm at a time and one drain at a time -- a single gemm_thread and
drain_thread over one physical array -- so a gemm cannot start until the
previous gemm has finished even when the two touch nothing in common. That is
often the real reason an instruction could not have run earlier: in one
scheduler_b run, gemm 22 accumulates into psum1 and its last psum1 writer,
gemm 19, ended at 376ns, yet gemm 22 started at 433ns, because gemm 20 held
the array until then. The load and store controllers are deliberately left
out: they pipeline across instructions (measured: the load controller overlaps
15 of 17 consecutive pairs in that same run), so "the previous one" is not a
constraint there.

CHOSEN edges are the ones a schedule imposes that neither the addresses nor
the machine require:
making an mvin wait for an unrelated gemm to keep it off that gemm's operand
banks, say. Nothing can derive those -- they are not in the addresses, and a
deliberate wait is indistinguishable in the timeline from an instruction that
simply happened to be issued later -- so a scheduler that makes such a decision
has to write it down itself (see ScheduleLog). They arrive here as the deps CSV,
minus whatever the other two kinds already explain.
"""

from collections import namedtuple
from pathlib import Path

# Two regions above the scratchpad's flat address space, so an access to one
# can never be mistaken for an access to another.
DRAM_BASE = 1 << 40
PSUM_BASE = 1 << 60

def parse_time(s: str):
    """The trace's "<ns>ns" timestamp, as an int when it is whole."""
    t = float(s.removesuffix("ns"))
    return int(t) if t.is_integer() else t


Range = namedtuple("Range", "lo stride count")
Access = namedtuple("Access", "range is_write")

READ, WRITE = False, True


def _contains(r: Range, x: int) -> bool:
    """Is x one of {lo, lo+stride, ..., lo+(count-1)*stride}?"""
    if r.count == 0 or x < r.lo:
        return False
    delta = x - r.lo
    if r.stride == 0:
        return delta == 0
    return delta % r.stride == 0 and delta // r.stride < r.count


def _overlap(a: Range, b: Range) -> bool:
    """Exact intersection of two arithmetic progressions -- walk the shorter
    one. A [lo,hi] bounding box would claim every address in between, which for
    the strided accesses this ISA generates invents conflicts that are not
    there."""
    if a.count == 0 or b.count == 0:
        return False
    few, many = (a, b) if a.count <= b.count else (b, a)
    return any(_contains(many, few.lo + i * few.stride) for i in range(few.count))


def _footprint(f: dict, dim: int) -> list:
    """What one instruction reads and writes, from its logged operands. A gemm or
    drain always covers DIM rows, which the trace's config line supplies."""
    op = f["op"]
    if op == "mvin":
        return [Access(Range(f["spad"], f["spad_stride"], f["rows"]), WRITE),
                Access(Range(DRAM_BASE + f["dram"], f["dram_stride"], f["rows"]), READ)]
    if op == "mvout":
        return [Access(Range(f["spad"], f["spad_stride"], f["rows"]), READ),
                Access(Range(DRAM_BASE + f["dram"], f["dram_stride"], f["rows"]), WRITE)]
    if op == "gemm":
        return [Access(Range(f["a"], f["a_stride"], dim), READ),
                Access(Range(f["b"], f["b_stride"], dim), READ),
                Access(Range(PSUM_BASE + f["psum"], 0, 1), WRITE)]
    if op == "gemm_flush":
        # No operand at all: it pushes zeros through the array. Its only
        # footprint is the accumulator it shifts into, which is enough to place
        # it after its own chain's gemms and before the drain that reads it.
        return [Access(Range(PSUM_BASE + f["psum"], 0, 1), WRITE)]
    if op == "drain":
        return [Access(Range(f["c"], f["c_stride"], dim), WRITE),
                Access(Range(PSUM_BASE + f["psum"], 0, 1), READ)]
    raise ValueError(f"unknown op {op!r}")


def parse_issues(trace_path: Path) -> list:
    """(id, op, footprint) for every instruction, in issue order."""
    issues = []
    dim = None
    with open(trace_path) as f:
        next(f, None)  # header
        for line in f:
            parts = line.rstrip("\n").split(",", 4)
            if len(parts) < 5:
                continue
            if parts[1] == "config":
                dim = int(dict(kv.partition("=")[::2] for kv in parts[4].split(","))["DIM"])
                continue
            if parts[1] != "top" or parts[3] != "issue":
                continue
            fields = {}
            for kv in parts[4].split(","):
                key, _, value = kv.partition("=")
                fields[key] = value if key == "op" else int(value)
            issues.append((int(parts[2]), fields["op"], _footprint(fields, dim)))
    return issues


def necessary(issues: list) -> dict:
    """id -> sorted ids it must follow, from the addresses alone."""
    edges = {}
    for i, (iid, _, accesses) in enumerate(issues):
        deps = set()
        for access in accesses:
            for j in range(i - 1, -1, -1):
                jid, _, earlier = issues[j]
                shadowed = False
                for other in earlier:
                    if not _overlap(access.range, other.range):
                        continue
                    if not access.is_write and not other.is_write:
                        continue  # read-after-read is not a dependency
                    deps.add(jid)
                    shadowed |= other.is_write
                if shadowed:
                    break  # the last writer hides everything before it
        edges[iid] = sorted(deps)
    return edges


# The engines that run one instruction at a time, and which op runs on which.
# Everything issued to one of these queues behind another has to wait for it,
# whatever it touches. gemm and gemm_flush share an engine: they drive the same
# datapath out of one queue, so a flush blocks the next gemm exactly as another
# gemm would. The load and store controllers are deliberately absent -- they
# pipeline across instructions, so "the previous one" is not a constraint.
SERIAL_ENGINES = {"gemm": "exec_gemm", "gemm_flush": "exec_gemm", "drain": "exec_drain"}


def structural(issues: list) -> dict:
    """id -> the previous instruction on the same single-issue engine."""
    edges = {}
    last = {}
    for iid, op, _ in issues:
        engine = SERIAL_ENGINES.get(op)
        edges[iid] = [last[engine]] if engine in last else []
        if engine:
            last[engine] = iid
    return edges


def parse_recorded(deps_path: Path) -> dict:
    """id -> (deps, name) as the scheduler wrote them down."""
    recorded = {}
    with open(deps_path) as f:
        next(f, None)  # header
        for line in f:
            parts = line.rstrip("\n").split(",", 2)
            if len(parts) < 3:
                continue
            deps = [int(d) for d in parts[1].split(";") if d]
            recorded[int(parts[0])] = (deps, parts[2])
    return recorded


def find_deps_path(trace_path: Path):
    """trace_scheduler_c.csv -> deps_scheduler_c.csv, if the scheduler wrote one."""
    name = trace_path.name
    if name.startswith("trace_"):
        candidate = trace_path.with_name("deps_" + name[len("trace_"):])
        if candidate.exists():
            return candidate
    return None


def build(trace_path: Path) -> dict:
    """id -> {t: type, d: every dependency, s: structural, c: chosen}."""
    issues = parse_issues(trace_path)
    must = necessary(issues)
    resource = structural(issues)
    types = {iid: op for iid, op, _ in issues}
    recorded, names = {}, {}
    deps_path = find_deps_path(trace_path)
    if deps_path:
        for iid, (deps, name) in parse_recorded(deps_path).items():
            recorded[iid] = deps
            names[iid] = name

    out = {}
    for iid, op in types.items():
        required = must.get(iid, [])
        resources = sorted(set(resource.get(iid, [])) - set(required))
        chosen = sorted(set(recorded.get(iid, [])) - set(required) - set(resources))
        out[iid] = {"t": op,
                    "d": sorted(set(required) | set(resources) | set(chosen)),
                    "s": resources,
                    "c": chosen,
                    # What the instruction does, not when it was issued: ids
                    # follow dispatch order and shift the moment a schedule is
                    # reordered, so the viewers label bars with this instead.
                    "n": names.get(iid, "")}
    return out


def summarise(graph: dict) -> str:
    structural_n = sum(len(v["s"]) for v in graph.values())
    chosen_n = sum(len(v["c"]) for v in graph.values())
    data_n = sum(len(v["d"]) for v in graph.values()) - structural_n - chosen_n
    return (f"{len(graph)} instructions, {data_n} data edge(s), "
            f"{structural_n} structural edge(s), {chosen_n} chosen edge(s)")
