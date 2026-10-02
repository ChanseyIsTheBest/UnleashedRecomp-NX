#!/usr/bin/env python3
"""List the game's message dispatchers for patches/message_dispatch.cpp ([Switch] SwitchNativeMessageDispatch).

The game's ProcessMessage overrides (MSVC output of one macro) test the message's type against a list:

    mflr r12 / bl __savegprlr_28 / stwu r1,-F(r1)
    mr r28,r5 / mr r30,r3 / mr r31,r4 / clrlwi. r29,r28,24 / beq ...      (r29: the flag byte)
    per type:  [cmplwi cr6,r29,0 / beq|bne cr6,...]                          (which list: flag 0 or not)
               mr r3,r31 / bl __RTtypeid / lis r11,.. / mr r4,r3 / addi r3,r11,.. / bl type_info==
               clrlwi. r11,r3,24 / beq <next type>
               mr r4,r31 / addi r3,r30,<adjust> / bl <handler> / (b <return>) | (li r3,1 / b <return>)
    at the end: mr r5,r28 / mr r4,r31 / mr r3,r30 / bl <base class ProcessMessage> / addi r1,r1,F / b __restgprlr_28

This script finds every function whose whole control flow, for both values of the flag, is made of exactly
these pieces, in the recompiled sources (UnleashedRecompLib/ppc, which carry each guest instruction as a
comment). It leaves out functions with a mid-asm hook or a hook in the app already. It writes their addresses
to UnleashedRecomp/patches/message_dispatch_list.inl; message_dispatch.cpp hooks each of them and, at startup,
decodes the same pattern again from the guest code in memory, keeping the recompiled function for any that
does not match. So this list only decides which functions get a hook; it cannot make one inexact.

Run it after the recompiled sources change (it needs them generated):
    python tools/switch-message-dispatch.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PPC_DIR = ROOT / "UnleashedRecompLib" / "ppc"
SWA_TOML = ROOT / "UnleashedRecompLib" / "config" / "SWA.toml"
APP_DIR = ROOT / "UnleashedRecomp"
OUTPUT = APP_DIR / "patches" / "message_dispatch_list.inl"

RTTYPEID = 0x831B2438
TYPEINFO_EQ = 0x831B0AB8
SAVEGPRLR_28 = 0x831B0B28
RESTGPRLR_28 = 0x831B0B78

DEFINITION = re.compile(r"^PPC_(?:HOT_)?FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\) \{$")
LABEL = re.compile(r"^loc_([0-9A-F]{8}):$")
INSTRUCTION = re.compile(r"^\t// (.*)$")
NOT_INSTRUCTIONS = ("callee-saved register store dropped (skipCalleeSaves)",)


def read_functions() -> dict[int, dict[int, str]]:
    """Guest address -> {instruction address: instruction text} for every recompiled function."""
    functions: dict[int, dict[int, str]] = {}
    files = sorted(PPC_DIR.glob("ppc_recomp.*.cpp"))
    if not files:
        sys.exit(f"No generated sources in {PPC_DIR}; run the build (XenonRecomp) first.")
    for path in files:
        current = None
        address = 0
        consistent = True
        for line in path.read_text(encoding="utf-8").splitlines():
            m = DEFINITION.match(line)
            if m:
                current = int(m.group(1), 16)
                address = current
                consistent = True
                functions[current] = {}
                continue
            if current is None:
                continue
            if line == "}":
                if not consistent:
                    del functions[current]
                current = None
                continue
            m = LABEL.match(line)
            if m:
                if int(m.group(1), 16) != address:
                    consistent = False
                continue
            m = INSTRUCTION.match(line)
            if m:
                text = m.group(1)
                if text in NOT_INSTRUCTIONS or text.startswith("ERROR"):
                    continue
                functions[current][address] = text
                address += 4
    return functions


def mid_asm_hooks() -> set[int]:
    text = SWA_TOML.read_text(encoding="utf-8")
    hooks = set()
    for block in text.split("[[midasm_hook]]")[1:]:
        m = re.search(r"^address\s*=\s*0x([0-9A-Fa-f]+)", block, re.MULTILINE)
        if m:
            hooks.add(int(m.group(1), 16))
    return hooks


def app_hooks() -> set[int]:
    """Functions the app already defines as sub_X (a second hook would not link)."""
    hooked = set()
    pattern = re.compile(r"(?<![\w])sub_([0-9A-Fa-f]{8})(?![0-9A-Fa-f])")
    for path in APP_DIR.rglob("*"):
        if path.suffix not in {".cpp", ".h", ".inl", ".hpp", ".c"} or path == OUTPUT:
            continue
        if path.name == "message_dispatch.cpp":
            continue
        for m in pattern.finditer(path.read_text(encoding="utf-8", errors="replace")):
            hooked.add(int(m.group(1), 16))
    return hooked


def target(text: str, mnemonic: str) -> int | None:
    """The target of `mnemonic 0x...`; a mnemonic ending in ',' (e.g. "beq cr6,") takes no space before it."""
    m = re.fullmatch(re.escape(mnemonic) + ("" if mnemonic.endswith(",") else " ") + r"0x([0-9a-f]+)", text)
    return int(m.group(1), 16) if m else None


def matches_template(fn: int, code: dict[int, str]) -> tuple[bool, str, int]:
    """Whether fn is a dispatcher of exactly the pattern above; the reason if not; its comparison count."""
    def at(a: int) -> str:
        return code.get(a, "")

    head = ["mflr r12", f"bl 0x{SAVEGPRLR_28:x}"]
    if [at(fn), at(fn + 4)] != head:
        return False, "prologue", 0
    m = re.fullmatch(r"stwu r1,-(\d+)\(r1\)", at(fn + 8))
    if not m:
        return False, "frame", 0
    frame = int(m.group(1))
    if [at(fn + 12), at(fn + 16), at(fn + 20), at(fn + 24)] != ["mr r28,r5", "mr r30,r3", "mr r31,r4", "clrlwi. r29,r28,24"]:
        return False, "arguments", 0
    first = target(at(fn + 28), "beq")
    if first is None:
        return False, "flag branch", 0

    def is_epilogue(a: int) -> bool:
        return at(a) == f"addi r1,r1,{frame}" and at(a + 4) == f"b 0x{RESTGPRLR_28:x}"

    def returns_one(a: int) -> bool:
        if at(a) != "li r3,1":
            return False
        b = target(at(a + 4), "b")
        return is_epilogue(a + 4) or (b is not None and is_epilogue(b))

    comparisons = 0
    for flag in (0, 1):
        pc = first if flag == 0 else fn + 32
        for _ in range(4096):
            text = at(pc)
            if text == "cmplwi cr6,r29,0":
                beq = target(at(pc + 4), "beq cr6,")
                bne = target(at(pc + 4), "bne cr6,")
                if beq is not None:
                    pc = beq if flag == 0 else pc + 8
                elif bne is not None:
                    pc = bne if flag != 0 else pc + 8
                else:
                    return False, f"flag test at {pc:08X}", 0
                continue
            if text == "mr r3,r31":
                block = [at(pc + 4 * i) for i in range(8)]
                if (block[1] != f"bl 0x{RTTYPEID:x}" or not re.fullmatch(r"lis r11,-?\d+", block[2]) or
                        block[3] != "mr r4,r3" or not re.fullmatch(r"addi r3,r11,-?\d+", block[4]) or
                        block[5] != f"bl 0x{TYPEINFO_EQ:x}" or block[6] != "clrlwi. r11,r3,24"):
                    return False, f"comparison at {pc:08X}", 0
                no_match = target(block[7], "beq")
                if no_match is None:
                    return False, f"comparison branch at {pc:08X}", 0
                call = pc + 32
                pair = {at(call), at(call + 4)}
                if "mr r4,r31" not in pair or not any(re.fullmatch(r"addi r3,r30,-?\d+", t) for t in pair):
                    return False, f"handler call at {call:08X}", 0
                if target(at(call + 8), "bl") is None:
                    return False, f"handler at {call:08X}", 0
                after = call + 12
                b = target(at(after), "b")
                if not ((b is not None and (is_epilogue(b) or returns_one(b))) or returns_one(after)):
                    return False, f"handler return at {after:08X}", 0
                comparisons += 1
                pc = no_match
                continue
            if text in ("mr r5,r28", "mr r4,r31", "mr r3,r30"):
                if sorted([at(pc), at(pc + 4), at(pc + 8)]) != ["mr r3,r30", "mr r4,r31", "mr r5,r28"]:
                    return False, f"tail at {pc:08X}", 0
                if target(at(pc + 12), "bl") is None or not is_epilogue(pc + 16):
                    return False, f"tail call at {pc:08X}", 0
                break
            return False, f"unexpected '{text}' at {pc:08X}", 0
        else:
            return False, "no end", 0
    return True, "", comparisons


def main() -> None:
    functions = read_functions()
    midasm = mid_asm_hooks()
    hooked = app_hooks()

    candidates = [fn for fn, code in functions.items() if sum(1 for t in code.values() if t == f"bl 0x{TYPEINFO_EQ:x}") >= 2]
    selected = []
    reasons: dict[str, int] = {}
    for fn in sorted(candidates):
        code = functions[fn]
        ok, why, comparisons = matches_template(fn, code)
        if ok and any(fn <= a <= max(code) for a in midasm):
            ok, why = False, "mid-asm hook"
        if ok and fn in hooked:
            ok, why = False, "hooked in the app"
        if ok and comparisons < 2:
            ok, why = False, "fewer than 2 comparisons"
        if ok:
            selected.append(fn)
        else:
            key = why.split(" at ")[0]
            reasons[key] = reasons.get(key, 0) + 1

    lines = [
        "// Generated by tools/switch-message-dispatch.py from the recompiled sources: the game's message",
        "// dispatchers (see patches/message_dispatch.cpp). Do not edit; run the script again instead.",
        "",
    ]
    lines += [f"MESSAGE_DISPATCHER({fn:08X})" for fn in selected]
    OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"switch-message-dispatch: {len(selected)} dispatchers of {len(candidates)} functions with 2+ type comparisons "
          f"written to {OUTPUT.relative_to(ROOT)}")
    for why, count in sorted(reasons.items(), key=lambda item: -item[1]):
        print(f"  left out: {count:4d}  {why}")


if __name__ == "__main__":
    main()
