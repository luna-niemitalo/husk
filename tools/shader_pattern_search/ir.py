"""Parse vkd3d-compiler d3d-asm text into a def-use dataflow graph.

Scope, deliberately narrow: this is a reverse-engineering aid for hunting
known math shapes (combiner/composite formulas) inside captured WoW
`ps_5_0`/`vs_5_0` shaders, not a general DXBC disassembler or a correctness
tool. Two simplifications, both safe for that goal but worth knowing about
before trusting a result blind:

- Dataflow is tracked per *register* (e.g. `r2`), not per component
  (`r2.x` vs `r2.y`) -- a write to any component of `r2` counts as the
  current definition of all of `r2`. Loses precision, never loses an edge.
- `loop`/`endloop` back-edges aren't modeled (a register written on a later
  iteration doesn't link back to an earlier one). Irrelevant for the small
  sample->blend->output chains this tool searches for; would matter for
  anything trying to trace the light-accumulation loops themselves.

See `patterns.py` for how a parsed shader's dataflow slices get matched
against known shapes, and `scan.py` for the corpus-wide CLI.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

_NO_DEST_OPCODES = {
    "if_nz", "if_z", "else", "endif", "loop", "endloop",
    "breakp_nz", "breakp_z", "discard_nz", "discard_z", "ret",
}

_REG_CLASS_PATTERNS = [
    (re.compile(r"^r\d+$"), "temp"),
    (re.compile(r"^x\d+$"), "temp"),  # indexable temps x0[...]/x1[...]
    (re.compile(r"^o(\d+|Mask|Depth)$"), "output"),
    (re.compile(r"^v\d+$"), "input"),
    (re.compile(r"^cb\d+$"), "cbuffer"),
    (re.compile(r"^t\d+$"), "texture"),
    (re.compile(r"^s\d+$"), "sampler"),
]


def reg_class(base: str) -> str:
    for pattern, cls in _REG_CLASS_PATTERNS:
        if pattern.match(base):
            return cls
    return "other"


@dataclass
class Operand:
    kind: str  # "reg" | "imm" | "null" | "raw"
    name: str  # base + index, e.g. "r2" or "cb0[11]" or "x0[0]"
    swizzle: str
    neg: bool = False
    absmod: bool = False
    base: str = ""
    index: str = ""

    @property
    def cls(self) -> str:
        return reg_class(self.base) if self.kind == "reg" else self.kind


def _split_top_level(line: str, sep_chars: str, opens: str = "([", closes: str = ")]") -> list[str]:
    parts, depth, cur = [], 0, ""
    for ch in line:
        if ch in opens:
            depth += 1
        elif ch in closes:
            depth -= 1
        if depth == 0 and ch in sep_chars:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


_OPERAND_RE = re.compile(r"^([a-zA-Z_]+\d*)((?:\[[^\]]*\])?)(\.[a-zA-Z]+)?$")


def parse_operand(tok: str) -> Operand:
    tok = tok.strip()
    # strip a [precise(...)] annotation glued onto the operand, if any
    tok = re.sub(r"\[precise\([^)]*\)\]\s*", "", tok)
    neg = tok.startswith("-")
    if neg:
        tok = tok[1:]
    absmod = tok.startswith("|") and tok.endswith("|")
    if absmod:
        tok = tok[1:-1]
    if tok.startswith("l("):
        return Operand(kind="imm", name=tok, swizzle="", neg=neg, absmod=absmod)
    if tok == "null":
        return Operand(kind="null", name="null", swizzle="", neg=neg, absmod=absmod)
    m = _OPERAND_RE.match(tok)
    if not m:
        return Operand(kind="raw", name=tok, swizzle="", neg=neg, absmod=absmod)
    base, idx, swz = m.groups()
    return Operand(
        kind="reg", name=base + idx, swizzle=(swz or "")[1:],
        neg=neg, absmod=absmod, base=base, index=idx,
    )


@dataclass
class Instruction:
    idx: int
    opcode: str
    dest: Operand | None
    srcs: list[Operand]
    raw: str
    depth: int


def _split_opcode_and_operands(line: str) -> tuple[str, str]:
    depth, i = 0, 0
    for i, ch in enumerate(line):
        if ch in "(":
            depth += 1
        elif ch in ")":
            depth -= 1
        elif ch == " " and depth == 0:
            return line[:i], line[i + 1:].strip()
    return line, ""


def parse_instruction(idx: int, raw_line: str, depth: int) -> Instruction | None:
    line = raw_line.strip()
    if not line or line.startswith("dcl_") or line.startswith(("ps_5_0", "vs_5_0")):
        return None
    line = re.sub(r"\[precise\([^)]*\)\]\s*", "", line)
    opcode, operand_str = _split_opcode_and_operands(line)
    tokens = [t.strip() for t in _split_top_level(operand_str, ",") if t.strip()]
    operands = [parse_operand(t) for t in tokens]

    opcode_base = opcode.split("(")[0]
    if opcode_base in _NO_DEST_OPCODES:
        return Instruction(idx, opcode_base, None, operands, line, depth)

    if opcode_base in ("imul", "umul") and operands and operands[0].kind == "null":
        # imul dstHi(discarded), dstLo, src0, src1
        if len(operands) < 2:
            return Instruction(idx, opcode_base, None, operands, line, depth)
        return Instruction(idx, opcode_base, operands[1], operands[2:], line, depth)

    if not operands:
        return Instruction(idx, opcode_base, None, [], line, depth)
    return Instruction(idx, opcode_base, operands[0], operands[1:], line, depth)


@dataclass
class ShaderIR:
    path: Path
    instructions: list[Instruction]
    edges: dict[int, set[int]]  # instr idx -> set of instr idxs it depends on
    output_writers: dict[str, list[int]]  # "o0"/"oMask" -> instr idxs that write it

    def by_idx(self, i: int) -> Instruction:
        return self._by_idx[i]

    def __post_init__(self):
        self._by_idx = {instr.idx: instr for instr in self.instructions}

    def slice_from(self, sink_idx: int) -> list[int]:
        """Exact backward-dependency closure of a single instruction -- every
        instruction sink_idx transitively depends on, plus itself. This is
        the general form; slice_for_output is just this seeded from an
        output write instead of an arbitrary instruction."""
        seen: set[int] = set()
        stack = [sink_idx]
        while stack:
            i = stack.pop()
            if i in seen:
                continue
            seen.add(i)
            stack.extend(self.edges.get(i, ()))
        return sorted(seen)

    def slice_for_output(self, output_name: str) -> list[list[int]]:
        """One backward-dependency slice per instruction that writes output_name."""
        return [self.slice_from(widx) for widx in self.output_writers.get(output_name, [])]

    def leaves_of(self, slice_idxs: list[int]) -> list[Operand]:
        """Free inputs of a slice: source operands read within it whose
        register class isn't 'temp' (texture/cbuffer/input/sampler reads --
        nothing inside the slice ever defines them, so from the slice's own
        point of view they're parameters). One entry per distinct name."""
        seen: dict[str, Operand] = {}
        idxset = set(slice_idxs)
        for i in slice_idxs:
            instr = self.by_idx(i)
            for src in instr.srcs:
                if src.kind == "reg" and src.cls != "temp" and src.name not in seen:
                    seen[src.name] = src
        return list(seen.values())


def parse_shader(path: Path) -> ShaderIR:
    lines = path.read_text().splitlines()
    instructions: list[Instruction] = []
    depth = 0
    idx = 0
    for raw_line in lines:
        stripped = raw_line.strip()
        is_closer = stripped.startswith(("endif", "endloop"))
        if is_closer:
            depth = max(depth - 1, 0)
        instr = parse_instruction(idx, raw_line, depth)
        is_opener = stripped.startswith(("if_nz", "if_z", "loop"))
        if is_opener:
            depth += 1
        if instr is None:
            continue
        instructions.append(instr)
        idx += 1

    last_writer: dict[str, int] = {}
    edges: dict[int, set[int]] = {}
    output_writers: dict[str, list[int]] = {}

    for instr in instructions:
        deps: set[int] = set()
        for src in instr.srcs:
            if src.kind == "reg" and src.cls == "temp" and src.name in last_writer:
                deps.add(last_writer[src.name])
        if deps:
            edges[instr.idx] = deps

        if instr.dest is not None and instr.dest.kind == "reg":
            cls = instr.dest.cls
            if cls == "temp":
                last_writer[instr.dest.name] = instr.idx
            elif cls == "output":
                output_writers.setdefault(instr.dest.base, []).append(instr.idx)

    return ShaderIR(path=path, instructions=instructions, edges=edges, output_writers=output_writers)
