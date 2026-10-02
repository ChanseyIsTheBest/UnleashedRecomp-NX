#!/usr/bin/env python3
"""[Switch] Round 15: compares, per recompiled function, the fused floating-point instructions in a built ELF
(fmadd/fmsub/fnmadd/fnmsub/fmla/fmls, any width) with the guest's own fused instructions (fmadd[s], fmsub[s],
fnmadd[s], fnmsub[s], vmaddfp[128], vnmsubfp[128], vmaddcfp128).

More in the ELF than in the guest means GCC contracted a separate multiply and add into one instruction (two roundings
became one). With SWITCH_EXPLICIT_FMA=1 there should be none: only the guest's fused instructions are fused.
(A function may hold fewer than the guest when GCC removed or merged code; and a function GCC inlined into its callers
counts there, so read the list, not only the totals.)

Usage (after a build, with the ppc/ sources it was built from):
    python tools/switch-fused-check.py build/switch-app/UnleashedRecomp/UnleashedRecomp
"""

from __future__ import annotations

import collections
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PPC_DIR = ROOT / "UnleashedRecompLib" / "ppc"
DEFINITION = re.compile(r"^PPC_(?:HOT_)?FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\) \{$")
GUEST_FUSED = {"fmadd", "fmadds", "fmsub", "fmsubs", "fnmadd", "fnmadds", "fnmsub", "fnmsubs", "vmaddfp", "vmaddfp128",
               "vnmsubfp", "vnmsubfp128", "vmaddcfp128"}


def objdump() -> str:
    devkitpro = os.environ.get("DEVKITPRO", "C:/devkitPro")
    for candidate in (Path(devkitpro) / "devkitA64" / "bin" / "aarch64-none-elf-objdump.exe",
                      Path(devkitpro) / "devkitA64" / "bin" / "aarch64-none-elf-objdump"):
        if candidate.exists():
            return str(candidate)
    return "aarch64-none-elf-objdump"


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    elf_path = sys.argv[1]

    guest: collections.Counter[str] = collections.Counter()
    for path in sorted(PPC_DIR.glob("ppc_recomp.*.cpp")):
        function = None
        for line in path.read_text(encoding="utf-8").splitlines():
            m = DEFINITION.match(line)
            if m:
                function = m.group(1)
                guest[function] += 0
                continue
            if function is None:
                continue
            if line.startswith("}"):
                function = None
                continue
            m = re.match(r"^\t// ([a-z][a-z0-9.]*)", line)
            if m and m.group(1).rstrip(".") in GUEST_FUSED:
                guest[function] += 1

    elf: collections.Counter[str] = collections.Counter()
    process = subprocess.Popen([objdump(), "-d", "--no-show-raw-insn", elf_path], stdout=subprocess.PIPE, text=True,
                               errors="replace")
    name = None
    for line in process.stdout:
        if line.endswith(">:\n"):
            m = re.search(r"<(?:__imp__)?sub_([0-9A-F]{8})>:$", line.rstrip("\n"))
            name = m.group(1) if m else None
            continue
        if name is not None and re.match(r"^\s+[0-9a-f]+:\s+(fmadd|fmsub|fnmadd|fnmsub|fmla|fmls)\s", line):
            elf[name] += 1
    process.wait()

    extra = {f: elf[f] - guest[f] for f in elf if elf[f] > guest[f]}
    print(f"functions with more fused instructions than the guest: {len(extra)}, extra fused instructions: "
          f"{sum(extra.values())}")
    for function, count in sorted(extra.items(), key=lambda item: -item[1])[:20]:
        print(f"  sub_{function}: ELF {elf[function]}, guest {guest[function]}")


if __name__ == "__main__":
    main()
