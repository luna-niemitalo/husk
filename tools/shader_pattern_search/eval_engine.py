"""Layer 2, part A: a small concrete interpreter for decomposed blocks.

Evaluates a block's instruction slice given concrete values for its free
inputs (texture samples / constant-buffer reads / etc, each a (x,y,z,w)
float tuple), producing the concrete value of its sink register. This is
what makes invariant-based testing (invariants.py) possible: two different
DXBC encodings of the same math produce the same *numbers* here, even
though patterns.py's textual matcher would only recognize one of them.

Deliberately narrow opcode coverage -- only what actually shows up in the
small texture-combine/composite slices this tool hunts for. An unsupported
opcode raises EvalUnsupported rather than guessing at a value; a block that
can't be evaluated is skipped, never silently mis-evaluated. This mirrors
husk's own foreign-data discipline (see CLAUDE.md's "Coding Policy: Foreign
Data" -- descriptive failure at the boundary, no defensive guessing past
it).

Known scope limits, not fixed here:
- No real control-flow modeling. A slice that spans an if/else merge point
  can pick up a definition from the "wrong" textual branch (same caveat
  ir.py's docstring already states for the dataflow graph itself). Blocks
  that live entirely inside one branch arm -- the common case for the
  composite motifs this hunts for -- aren't affected.
- int/uint ops (ftou/utof/itof, bitwise and/or) are treated as float
  pass-throughs/approximations, not bit-exact. Fine for color math, would
  need real integer semantics for anything index/bitmask-driven.
"""
from __future__ import annotations

import math

from ir import Instruction, Operand, ShaderIR

Vec4 = list  # 4 floats

_COMP = {"x": 0, "y": 1, "z": 2, "w": 3}


class EvalUnsupported(Exception):
    def __init__(self, opcode: str):
        super().__init__(f"unsupported opcode for evaluation: {opcode}")
        self.opcode = opcode


def _parse_immediate(name: str) -> list[float]:
    inner = name[len("l("):-1]
    vals = []
    for tok in inner.split(","):
        tok = tok.strip()
        try:
            if tok.lower().startswith("0x"):
                vals.append(float(int(tok, 16)))
            else:
                vals.append(float(tok))
        except ValueError:
            vals.append(0.0)
    return vals


def _clamp01(v: list[float]) -> list[float]:
    return [min(1.0, max(0.0, x)) for x in v]


def read_operand(op: Operand, width: int, state: dict[str, Vec4], assignments: dict[str, Vec4]) -> list[float]:
    if op.kind == "imm":
        vals = _parse_immediate(op.name)
        if len(vals) == 1:
            vals = vals * width
        result = (vals + [0.0] * width)[:width]
    elif op.kind == "reg":
        base = state.get(op.name)
        if base is None:
            base = assignments.get(op.name)
        if base is None:
            raise EvalUnsupported(f"unresolved register {op.name}")
        swz = op.swizzle or "xyzw"
        if len(swz) == 1:
            swz = swz * width
        result = [base[_COMP[swz[i]]] for i in range(width)]
    else:
        raise EvalUnsupported(f"operand kind {op.kind}")
    if op.neg:
        result = [-v for v in result]
    if op.absmod:
        result = [abs(v) for v in result]
    return result


def write_operand(dest: Operand, values: list[float], state: dict[str, Vec4]) -> None:
    wm = dest.swizzle or "xyzw"
    vec = state.setdefault(dest.name, [0.0, 0.0, 0.0, 0.0])
    for i, letter in enumerate(wm):
        vec[_COMP[letter]] = values[i]


def eval_instr(instr: Instruction, state: dict[str, Vec4], assignments: dict[str, Vec4]) -> None:
    if instr.dest is None or instr.dest.cls != "temp":
        return  # control-flow-only or non-temp writes: not modeled, see module docstring

    op = instr.opcode
    sat = op.endswith("_sat")
    base_op = op[:-4] if sat else op
    width = len(instr.dest.swizzle) if instr.dest.swizzle else 4

    def src(i: int, w: int = width) -> list[float]:
        return read_operand(instr.srcs[i], w, state, assignments)

    if base_op == "mov":
        res = src(0)
    elif base_op == "mad":
        a, b, c = src(0), src(1), src(2)
        res = [a[i] * b[i] + c[i] for i in range(width)]
    elif base_op == "mul":
        a, b = src(0), src(1)
        res = [a[i] * b[i] for i in range(width)]
    elif base_op == "add":
        a, b = src(0), src(1)
        res = [a[i] + b[i] for i in range(width)]
    elif base_op == "min":
        a, b = src(0), src(1)
        res = [min(a[i], b[i]) for i in range(width)]
    elif base_op == "max":
        a, b = src(0), src(1)
        res = [max(a[i], b[i]) for i in range(width)]
    elif base_op == "div":
        a, b = src(0), src(1)
        res = [a[i] / b[i] if b[i] != 0 else math.nan for i in range(width)]
    elif base_op == "rsq":
        a = src(0)
        res = [1.0 / math.sqrt(v) if v > 0 else math.nan for v in a]
    elif base_op == "rcp":
        a = src(0)
        res = [1.0 / v if v != 0 else math.nan for v in a]
    elif base_op == "sqrt":
        a = src(0)
        res = [math.sqrt(v) if v >= 0 else math.nan for v in a]
    elif base_op == "log":
        a = src(0)
        res = [math.log2(v) if v > 0 else math.nan for v in a]
    elif base_op == "exp":
        a = src(0)
        res = [2.0 ** v for v in a]
    elif base_op in ("dp3", "dp4"):
        n = 3 if base_op == "dp3" else 4
        a, b = src(0, n), src(1, n)
        s = sum(a[i] * b[i] for i in range(n))
        res = [s] * width
    elif base_op == "movc":
        cond, a, b = src(0), src(1), src(2)
        res = [a[i] if cond[i] != 0 else b[i] for i in range(width)]
    elif base_op in ("lt", "ge", "eq", "ne", "ult", "uge"):
        a, b = src(0), src(1)
        cmp = {"lt": lambda x, y: x < y, "ge": lambda x, y: x >= y,
               "eq": lambda x, y: x == y, "ne": lambda x, y: x != y,
               "ult": lambda x, y: x < y, "uge": lambda x, y: x >= y}[base_op]
        res = [1.0 if cmp(a[i], b[i]) else 0.0 for i in range(width)]
    elif base_op in ("ftou", "utof", "itof"):
        res = src(0)  # approximation: no real int reinterpretation, see docstring
    elif base_op == "sample_indexable":
        res = src(1)  # srcs[0]=uv (ignored), srcs[1]=texture+swizzle, srcs[2]=sampler (ignored)
    else:
        raise EvalUnsupported(op)

    if sat and base_op not in ("lt", "ge", "eq", "ne", "ult", "uge"):
        res = _clamp01(res)
    write_operand(instr.dest, res, state)


def evaluate_block(ir: ShaderIR, slice_idxs: list[int], sink_name: str, assignments: dict[str, Vec4],
                    sink_swizzle: str = "xyzw") -> list[float]:
    """Concretely evaluate a block. Returns the values in sink_swizzle's
    write order (e.g. ".yzw" -> [state[1], state[2], state[3]]), NOT a
    fixed [0:3] slice -- the sink instruction's actual destination
    writemask isn't always "xyz" (see decompose.py's sink_swizzle
    comment), and comparing the wrong raw slots silently compares the
    wrong numbers. Raises EvalUnsupported if any instruction in the slice
    uses an opcode this interpreter doesn't model -- callers should treat
    that as "block not evaluable," not retry with a guess."""
    state: dict[str, Vec4] = {}
    for i in slice_idxs:
        eval_instr(ir.by_idx(i), state, assignments)
    if sink_name not in state:
        raise EvalUnsupported(f"sink register {sink_name} never written within slice")
    vec = state[sink_name]
    return [vec[_COMP[letter]] for letter in sink_swizzle]
