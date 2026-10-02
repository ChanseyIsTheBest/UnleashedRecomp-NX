#!/usr/bin/env python3
"""[Switch] Round 11 code generation options, applied to the recompiled sources right after XenonRecomp writes them.

Each option is a build switch (tools/build-switch.sh, environment variables, default 0) and changes only how the
compiler gets to see the code, never what the guest code does:

  SWITCH_LEAF_LOCALS=1       In a function that calls nothing (a leaf: no direct, indirect or tail call, no mid-asm
                             hook, nothing else using the context), the context's registers (r1, r3-r10, r13, f1-f13,
                             v0-v13) become locals: read from the PPCContext at entry, written back at every return,
                             each one the function may write. The values at return are the same; in between, the
                             registers are plain locals, which no guest store or loop barrier forces to memory.
  SWITCH_WIDE_DFORM=1        Loads and stores at register + displacement (the D form) as a 64-bit address
                             (PPC_LOAD_U32_D...), for displacements of 0 to 4095 or any from r1; see ppc_context.h
                             and the guard page in kernel/memory.cpp.
  SWITCH_CONST_VMX_TABLES=1  VectorMaskL/R and the shift tables as constants, and the byte shuffles that use
                             VectorMaskL/R as a plain NEON TBL (PPC_VECTOR_TABLE); see ppc_context.h.
  SWITCH_NARROW_BARRIER=1    The loop barriers on guest memory only; see ppc_context.h. (The compare-and-swap's
                             barriers sit inside basic blocks and stay full.)
  SWITCH_INLINE_FP_COMPARE=1 Round 12: fcmpu/fcmpo's PPCCRRegister::compare(double, double) branchless and always
                             inlined (a define in ppc_config.h only; the sources are not touched). A comparison has no
                             arithmetic to round, so it applies to every function.
  SWITCH_INLINE_MEMCPY=1     Round 14: a call of the hooked memcpy or memset (misc_impl.cpp) whose size the guest code
                             loads as a constant right before it (li r5,N, with nothing but straight-line statements
                             that leave r5 alone in between) becomes __builtin_memcpy/__builtin_memset of that size, on
                             the call's own line: the hook did the same host call with the same pointers (base + r3,
                             base + r4) and set r3 to r3's low 32 bits, which the line does too. GCC then copies small
                             fixed sizes with a few loads and stores instead of the hook and the library's size
                             dispatch. Only overlapping copies, undefined for memcpy, could come out differently, and
                             the game's fixed-size copies are struct copies between distinct (or identical) objects.
                             The other calls of the hooked memcpy, memmove and memset (size in r5 at run time) call
                             the C library's function the hook called, with the same arguments, without the hook's
                             argument marshalling. Applied before SWITCH_LEAF_LOCALS, so a function whose only calls
                             were these becomes a leaf.

Round 15: with SWITCH_EXPLICIT_FMA=1 (the guest's fused multiply-adds explicit, everything compiled with
-ffp-contract=off) GCC fuses nothing on its own, so the options apply to every function; without it, as follows.
A function with double-precision or vector floating-point arithmetic is left exactly as XenonRecomp wrote it (and
keeps the full loop barrier): GCC fuses a multiply and a later add when it sees the product's value flow straight
into the add (-ffp-contract=fast; within a basic block, which PGO's loop unrolling can merge), and each option lets
it see more of that flow, which could change how such a sum is rounded. Single-precision instructions round their
result to float, which no add can be fused across, and the product of two singles is exact in double, so whether
GCC fuses the multiply-add inside one of them gives the same result; integer code has nothing to fuse.

The ppc/ folder is regenerated whenever these change (build-switch.sh puts them in its code generation id), so this
runs on XenonRecomp's own output. It writes ppc/codegen_pass.txt with what it applied and refuses to run twice.

Usage: python tools/switch-codegen-pass.py   (reads the six variables)
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PPC_DIR = Path(os.environ.get("SWITCH_CODEGEN_PPC_DIR", ROOT / "UnleashedRecompLib" / "ppc"))  # override: tests
SWA_TOML = ROOT / "UnleashedRecompLib" / "config" / "SWA.toml"
STAMP = PPC_DIR / "codegen_pass.txt"

DEFINITION = re.compile(r"^PPC_(?:HOT_)?FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\) \{$")
CONTEXT_REGISTER = re.compile(r"\bctx\.((?:r|f|v)\d+)\b")
CALL = re.compile(r"(?<![\w])(?:__imp__)?sub_[0-9A-F]{8}\(ctx, base\)")
LOCALIZABLE = {f"r{i}" for i in (1, 3, 4, 5, 6, 7, 8, 9, 10, 13)} | {f"f{i}" for i in range(1, 14)} | {f"v{i}" for i in range(14)}
MNEMONIC = re.compile(r"^\t// ([a-z][a-z0-9.]*)")
DOUBLE_ARITHMETIC = {"fadd", "fsub", "fmul", "fmadd", "fmsub", "fnmadd", "fnmsub"}


def floating_point_sensitive(body: list[str]) -> bool:
    """Double-precision arithmetic, or any vector floating-point instruction (conservatively, every v...fp...)."""
    for line in body:
        m = MNEMONIC.match(line)
        if m:
            mnemonic = m.group(1).rstrip(".")
            if mnemonic in DOUBLE_ARITHMETIC or (mnemonic.startswith("v") and "fp" in mnemonic):
                return True
    return False


def option(name: str) -> bool:
    return os.environ.get(name, "0") == "1"


def mid_asm_hook_names() -> list[str]:
    return re.findall(r'^name\s*=\s*"(\w+)"', SWA_TOML.read_text(encoding="utf-8"), re.MULTILINE)


# ------------------------------------------------------------------------------------------------ leaf locals

def written_registers(body: list[str], registers: set[str]) -> set[str]:
    """Registers a statement may write: anything but a plain read of a field counts (so at worst an unchanged
    value is written back, never a changed one missed)."""
    written = set()
    assignment = r"(?!\s*(?:=(?!=)|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<=|>>=|\+\+|--))"
    # A scalar register's field, read; a vector register's element (with an index), read. A vector field without
    # an index is an array that decays to a pointer (simde loads and stores take them so): counted as written.
    scalar_read = re.compile(r"ctx\.(?:r|f)\d+\.(?:u8|u16|u32|u64|s8|s16|s32|s64|f32|f64)\b" + assignment)
    vector_read = re.compile(r"ctx\.v\d+\.(?:u8|u16|u32|u64|s8|s16|s32|s64|f32|f64)\[[^\]]*\]" + assignment)
    for line in body:
        if line.lstrip().startswith("//"):
            continue
        for m in CONTEXT_REGISTER.finditer(line):
            register = m.group(1)
            start = m.start()
            before = line[max(0, start - 2):start]
            plain = (vector_read if register[0] == "v" else scalar_read).match(line, start)
            if not plain or before.endswith("*)") or before.endswith("&") or line[start - 1:start] == "(" and before.endswith("&("):
                written.add(register)
    return written & registers


def localize_leaf(name: str, body: list[str], hooks: list[str]) -> list[str] | None:
    code_lines = [line for line in body if not line.lstrip().startswith("//")]
    text = "\n".join(code_lines)
    if CALL.search(text) or "PPC_CALL_INDIRECT_FUNC" in text or "setjmp" in text or "longjmp" in text:
        return None
    if any(hook + "(" in text for hook in hooks):
        return None
    rest = CONTEXT_REGISTER.sub("", text).replace("ctx.fpscr", "")
    if re.search(r"\bctx\b", rest):
        return None

    registers = set(CONTEXT_REGISTER.findall(text))
    if not registers or not registers <= LOCALIZABLE:
        return None
    # The local names must be free.
    if any(re.search(rf"\bPPC(?:V)?Register {register}\b", text) for register in registers):
        return None

    def order(register: str) -> tuple[int, int]:
        return ("rfv".index(register[0]), int(register[1:]))

    used = sorted(registers, key=order)
    written = sorted(written_registers(body, registers), key=order)
    write_back = " ".join(f"ctx.{r} = {r};" for r in written)

    if body[0].strip() != "PPC_FUNC_PROLOGUE();":
        return None
    out = [body[0], "\t// switch-codegen-pass: leaf locals"]  # PPC_FUNC_PROLOGUE();
    out += [f"\t{'PPCVRegister' if r[0] == 'v' else 'PPCRegister'} {r} = ctx.{r};" for r in used]
    for line in body[1:]:
        if line.lstrip().startswith("//"):
            out.append(line)
            continue
        line = CONTEXT_REGISTER.sub(r"\1", line)
        if written:
            line = re.sub(r"(?<![\w])return;", "{ " + write_back + " return; }", line)
        out.append(line)
    if written:
        out.append("\t" + write_back)
    return out


# ------------------------------------------------------------------------------------------------ wide D form

LOAD_D = re.compile(r"PPC_LOAD_U(8|16|32|64)\(((?:ctx\.)?r(\d+))\.u32 \+ (-?\d+)\)")
STORE_D = re.compile(r"PPC_STORE_U(8|16|32|64)\(((?:ctx\.)?r(\d+))\.u32 \+ (-?\d+), ")


def wide_ok(register: str, displacement: int) -> bool:
    return 0 <= displacement <= 4095 or register == "1"


def wide_dform(line: str) -> str:
    def load(m: re.Match) -> str:
        if not wide_ok(m.group(3), int(m.group(4))):
            return m.group(0)
        return f"PPC_LOAD_U{m.group(1)}_D({m.group(2)}.u32, {m.group(4)})"

    def store(m: re.Match) -> str:
        if not wide_ok(m.group(3), int(m.group(4))):
            return m.group(0)
        return f"PPC_STORE_U{m.group(1)}_D({m.group(2)}.u32, {m.group(4)}, "

    return STORE_D.sub(store, LOAD_D.sub(load, line))


# ------------------------------------------------------------------------------------------------ vector tables

TABLE_INDEX = ("simde_mm_load_si128((simde__m128i*)VectorMaskL)", "simde_mm_load_si128((simde__m128i*)&VectorMaskL[",
               "simde_mm_load_si128((simde__m128i*)VectorMaskR)", "simde_mm_load_si128((simde__m128i*)&VectorMaskR[")
TABLE_NAME = re.compile(r"\b(VectorMaskL|VectorMaskR|VectorShiftTableL|VectorShiftTableR)\b")


def vector_tables(line: str) -> str:
    out = []
    i = 0
    name = "simde_mm_shuffle_epi8("
    while True:
        j = line.find(name, i)
        if j < 0:
            out.append(line[i:])
            return "".join(out)
        # The argument list, and its top-level comma.
        depth, k, comma = 1, j + len(name), -1
        while k < len(line) and depth > 0:
            c = line[k]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == "," and depth == 1 and comma < 0:
                comma = k
            k += 1
        second = line[comma + 1:k - 1].strip() if comma >= 0 else ""
        out.append(line[i:j])
        out.append("PPC_VECTOR_TABLE(" if any(second.startswith(t) for t in TABLE_INDEX) else name)
        i = j + len(name)


def constant_tables(line: str) -> str:
    """The constant copies of the tables (ppc_context.h), after vector_tables recognised the shuffles."""
    return TABLE_NAME.sub(r"\1Const", line)


# ------------------------------------------------------------------------------------------------ fixed-size memcpy

MEMCPY_HOOKS = {"831B0ED0", "831CCB98", "831CEAE0", "831CEE04", "831CF2D0", "831CF660", "831B1358"}
MEMSET_HOOKS = {"831B0BA0", "831CCAA0"}
MEMMOVE_HOOKS = {"831B5E00"}
GUEST_CALL = re.compile(r"^\t(?:__imp__)?sub_([0-9A-F]{8})\(ctx, base\);$")
FIXED_SIZE = re.compile(r"^\tctx\.r5\.s64 = (\d+);$")
STRAIGHT = re.compile(r"^\t[^;{}:?]*;$")
CONTROL = re.compile(r"\b(?:goto|return|if|else|while|for|switch|sub_[0-9A-F]{8}|__imp__sub_[0-9A-F]{8}|PPC_CALL\w*|setjmp|longjmp)\b")


def inline_fixed_copies(body: list[str]) -> tuple[list[str], int, int]:
    out = list(body)
    count = direct = 0
    for i, line in enumerate(body):
        m = GUEST_CALL.match(line)
        if not m or m.group(1) not in MEMCPY_HOOKS | MEMSET_HOOKS | MEMMOVE_HOOKS:
            continue
        if m.group(1) in MEMMOVE_HOOKS:
            out[i] = "\t__builtin_memmove(base + ctx.r3.u32, base + ctx.r4.u32, ctx.r5.u64); ctx.r3.u64 = ctx.r3.u32;"
            direct += 1
            continue
        size = None
        for j in range(i - 1, -1, -1):
            prev = body[j]
            if prev.lstrip().startswith("//"):
                continue
            s = FIXED_SIZE.match(prev)
            if s:
                size = int(s.group(1))
                break
            # Anything but a plain statement that leaves r5 alone (a label, a branch, a call, a statement that hands
            # the whole context to something) ends the search without a size.
            if not STRAIGHT.match(prev) or "r5" in prev or CONTROL.search(prev):
                break
            if re.search(r"\bctx\b", CONTEXT_REGISTER.sub("", prev).replace("ctx.fpscr", "")):
                break
        length = str(size) if size is not None else "ctx.r5.u64"
        if m.group(1) in MEMCPY_HOOKS:
            call = f"__builtin_memcpy(base + ctx.r3.u32, base + ctx.r4.u32, {length});"
        else:
            call = f"__builtin_memset(base + ctx.r3.u32, ctx.r4.s32, {length});"
        out[i] = f"\t{call} ctx.r3.u64 = ctx.r3.u32;"
        if size is not None:
            count += 1
        else:
            direct += 1
    return out, count, direct


# ------------------------------------------------------------------------------------------------ main

def main() -> None:
    options = {
        "SWITCH_LEAF_LOCALS": option("SWITCH_LEAF_LOCALS"),
        "SWITCH_WIDE_DFORM": option("SWITCH_WIDE_DFORM"),
        "SWITCH_CONST_VMX_TABLES": option("SWITCH_CONST_VMX_TABLES"),
        "SWITCH_NARROW_BARRIER": option("SWITCH_NARROW_BARRIER"),
        "SWITCH_INLINE_FP_COMPARE": option("SWITCH_INLINE_FP_COMPARE"),
        "SWITCH_INLINE_MEMCPY": option("SWITCH_INLINE_MEMCPY"),
    }
    summary = " ".join(f"{k}={int(v)}" for k, v in options.items())
    if option("SWITCH_EXPLICIT_FMA"):
        summary += " SWITCH_EXPLICIT_FMA=1"
    if STAMP.exists():
        applied = STAMP.read_text(encoding="utf-8").strip()
        if applied != summary:
            sys.exit(f"switch-codegen-pass: ppc/ already has '{applied}', not '{summary}'; regenerate it")
        print(f"switch-codegen-pass: already applied ({summary})")
        return
    if not any(options.values()):
        STAMP.write_text(summary + "\n", encoding="utf-8")
        print("switch-codegen-pass: nothing to apply")
        return

    # Round 15: not an option of this pass (XenonRecomp writes the fused operations), but it decides what the pass
    # may touch, so it is part of the stamp below.
    explicit_fma = option("SWITCH_EXPLICIT_FMA")
    hooks = mid_asm_hook_names()
    files = sorted(PPC_DIR.glob("ppc_recomp.*.cpp"), key=lambda p: int(p.name.split(".")[1]))
    if not files:
        sys.exit("switch-codegen-pass: no generated sources")

    leaves = wide = tables = sensitive = copies = directs = 0
    for path in files:
        lines = path.read_text(encoding="utf-8").split("\n")
        out = []
        i = 0
        while i < len(lines):
            line = lines[i]
            m = DEFINITION.match(line)
            if not m:
                out.append(line)
                i += 1
                continue
            end = lines.index("}", i)
            body = lines[i + 1:end]
            if not explicit_fma and floating_point_sensitive(body):
                if options["SWITCH_NARROW_BARRIER"]:
                    body = [b.replace("PPC_LOOP_BARRIER()", "PPC_LOOP_BARRIER_FULL()") if not b.lstrip().startswith("//") else b for b in body]
                sensitive += 1
                out.append(line)
                out += body
                out.append("}")
                i = end + 1
                continue
            if options["SWITCH_INLINE_MEMCPY"]:
                body, inlined, direct = inline_fixed_copies(body)
                copies += inlined
                directs += direct
            if options["SWITCH_LEAF_LOCALS"]:
                localized = localize_leaf(m.group(1), body, hooks)
                if localized is not None:
                    body = localized
                    leaves += 1
            if options["SWITCH_WIDE_DFORM"]:
                new = [wide_dform(b) if not b.lstrip().startswith("//") else b for b in body]
                wide += sum(a != b for a, b in zip(body, new))
                body = new
            if options["SWITCH_CONST_VMX_TABLES"]:
                new = [constant_tables(vector_tables(b)) if not b.lstrip().startswith("//") else b for b in body]
                tables += sum(a != b for a, b in zip(body, new))
                body = new
            out.append(line)
            out += body
            out.append("}")
            i = end + 1
        text = "\n".join(out)
        if text != path.read_text(encoding="utf-8"):
            path.write_text(text, encoding="utf-8", newline="\n")

    config = PPC_DIR / "ppc_config.h"
    defines = []
    if options["SWITCH_CONST_VMX_TABLES"]:
        defines.append("#define PPC_CONFIG_CONST_VMX_TABLES")
    if options["SWITCH_NARROW_BARRIER"]:
        defines.append("#define PPC_CONFIG_NARROW_BARRIER")
    if options["SWITCH_INLINE_FP_COMPARE"]:
        defines.append("#define PPC_CONFIG_INLINE_FP_COMPARE")
    if defines:
        text = config.read_text(encoding="utf-8")
        anchor = "#ifdef PPC_INCLUDE_DETAIL"
        assert anchor in text, "ppc_config.h layout"
        text = text.replace(anchor, "// switch-codegen-pass\n" + "\n".join(defines) + "\n\n" + anchor)
        config.write_text(text, encoding="utf-8", newline="\n")

    STAMP.write_text(summary + "\n", encoding="utf-8")
    print(f"switch-codegen-pass: {summary}: {copies} fixed-size memcpy/memset calls inlined and {directs} others sent "
          f"straight to the C library, {leaves} leaf functions "
          f"with local registers, {wide} lines with wide D-form accesses, {tables} lines with table shuffles; "
          f"{sensitive} functions with double-precision or vector floating-point arithmetic left as they were")


if __name__ == "__main__":
    main()
