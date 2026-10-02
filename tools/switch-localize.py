#!/usr/bin/env python3
"""Recompiled functions with their registers in locals, for patches/audio_dsp_patches.cpp ([Switch] round 11) and
patches/native_r15_localized.cpp (round 15).

The recompiled code keeps the guest's argument and volatile registers (r3-r10, f1-f13, ...) in the PPCContext. In a
loop every backward branch has a compiler barrier (PPC_LOOP_BARRIER, so that wait loops re-read guest memory), and
the barrier makes GCC write those context fields back to memory and read them again on every pass. The CRI sound
mixer's per-sample kernels are such loops.

This script copies chosen functions from the recompiled sources and changes one thing: every `ctx.<register>` becomes
`regs.<register>`, a field of a LocalRegs object the hook keeps on its own stack. Its address never escapes, so GCC
keeps it in registers, and the barriers (which only concern memory) no longer touch it. The statements are the
recompiled code's own, in the same order: the same guest loads and stores and the same floating-point expressions.
The context is written back before any call that leaves the copied code and read again after it, and written back by
the hook when the function returns, so everything else sees the context exactly as the recompiled function leaves it.

The audio group refuses a function whose floating-point work is not single precision (each fmadds/fmuls/... product
of two singles is exact in double, so how GCC contracts the multiply-adds cannot change a bit; double-precision or
vector floating-point arithmetic could round differently) or that uses the context in any other way.

Round 15: a loop of a copied function can be replaced by hand-written code (LOOP_REPLACEMENTS), chosen at run time
by a flag the including file defines (the copied loop stays as the other branch). The script checks that the guest
instructions of the loop are exactly the ones the replacement was written for, and refuses otherwise.

Round 15, the main-thread group (SwitchNativeSplineAnimation, SwitchNativePathFollowing): a hot function with the
helpers it calls copied into it (INLINED), so that the whole cluster runs with its registers in locals and without
the helpers' calls, prologues and context traffic. The FPSCR's cached mode is a local too, so GCC sees which flush
mode is set and drops a helper's switch to the mode the caller is already in (each switch is still made where the
mode changes). Any floating-point work is copied: these copies are only compiled with explicit fused multiply-adds
and -ffp-contract=off (SWITCH_EXPLICIT_FMA), where every statement rounds as written, in the copy as in the original
(the including file refuses to build them otherwise).

The outputs (UnleashedRecompLib/switch/localized_*.inl) are included twice by their files: as is, and with the guest
stores logged and calls leaving the copied code aborted, for the verify modes. They are kept outside UnleashedRecomp/
on purpose: switch-direct-calls.py counts any address written there as hooked. build-switch.sh runs this script after
the recompiled sources are generated, so the copies are always made from the build's own code.

Run after the recompiled sources change:
    python tools/switch-localize.py
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PPC_DIR = ROOT / "UnleashedRecompLib" / "ppc"
SWA_TOML = ROOT / "UnleashedRecompLib" / "config" / "SWA.toml"

DEFINITION = re.compile(r"^PPC_(?:HOT_)?FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\) \{$")
REGISTER = re.compile(r"\bctx\.((?:r|f|v)\d+)\b")
CALL = re.compile(r"(?<![\w])(?:__imp__)?sub_([0-9A-F]{8})\(ctx, base\);")
INDIRECT = re.compile(r"PPC_CALL_INDIRECT_FUNC\(([^;]*)\);")
ALLOWED_FP = {
    "lfs", "lfsx", "lfsu", "lfsux", "stfs", "stfsx", "stfsu", "stfsux", "lfd", "lfdx", "lfdu", "lfdux", "stfd", "stfdx",
    "stfdu", "stfdux", "stfiwx", "fmr", "fneg", "fabs", "fnabs", "fcmpu", "fcmpo", "fsel", "frsp", "fcfid", "fctid",
    "fctidz", "fctiw", "fctiwz", "fadds", "fsubs", "fmuls", "fdivs", "fmadds", "fmsubs", "fnmadds", "fnmsubs",
    "mffs", "mtfsf", "mtfsfi", "mtfsb0", "mtfsb1",
}

# [Switch] Round 15, SwitchFastAudioResampler: the CRI resampler's inner loop (guest loc_8315AE14 to loc_8315AE5C, a
# linear interpolation between two input samples per output sample). Every floating-point value in it is a single (lfs
# results and single-precision arithmetic: f31 and f0 are loaded with lfs before it, f13 with lfs or copied from the
# lfs-loaded f28, f29 and f30 are lfs-loaded constants), and the recompiled loop converts each result to single and back
# to double between steps. Here each fsubs/fadds/fmuls is one single-precision operation, which gives the guest's
# value: the product of two singles is exact in double, and rounding to double then to single is the same as rounding
# to single once for the sum or difference of two singles (53 >= 2 * 24 + 2; in the directed modes too). The fmadds
# stays the guest's double fused multiply-add of the same operands rounded to single, and the compares of singles are
# those of their doubles. Same loads and stores in the same order, the same loop barriers; at the end every register
# the loop writes holds what the guest's loop leaves (cr6 and temp are rewritten right after the loop before any read).
RESAMPLER_LOOP_GUEST = [
    "fcmpu cr6,f31,f30", "blt cr6,0x8315ae40", "cmplw cr6,r10,r7", "bge cr6,0x8315ae40", "fsubs f31,f31,f30",
    "addi r10,r10,1", "fmr f13,f0", "lfs f0,0(r11)", "addi r11,r11,4", "fcmpu cr6,f31,f30", "bge cr6,0x8315ae1c",
    "fsubs f12,f30,f31", "addic. r8,r8,-1", "fmuls f11,f0,f31", "fadds f31,f31,f29", "fmadds f10,f12,f13,f11",
    "stfs f10,0(r9)", "addi r9,r9,4", "bne 0x8315ae14",
]

RESAMPLER_LOOP = """\
	{
		float phase = float(f31.f64);
		const float one = float(f30.f64);
		const float step = float(f29.f64);
		float current = float(f0.f64);
		float previous = float(regs.f13.f64);
		float oneMinusPhase, weighted, sample;
		uint64_t index = regs.r10.u64;
		const uint32_t end = regs.r7.u32;
		uint64_t input = r11.u64;
		uint64_t output = regs.r9.u64;
		uint64_t count = regs.r8.u64;
		for (;;)
		{
			if (!(phase < one))
			{
				while (uint32_t(index) < end)
				{
					phase = phase - one;
					index++;
					previous = current;
					current = std::bit_cast<float>(uint32_t(PPC_LOAD_U32_D(uint32_t(input), 0)));
					input += 4;
					if (phase < one)
						break;
					PPC_LOOP_BARRIER();
				}
			}
			oneMinusPhase = one - phase;
			count--;
			weighted = current * phase;
			phase = phase + step;
			sample = float(__builtin_fma(double(oneMinusPhase), double(previous), double(weighted)));
			PPC_STORE_U32_D(uint32_t(output), 0, std::bit_cast<uint32_t>(sample));
			output += 4;
			if (uint32_t(count) == 0)
				break;
			PPC_LOOP_BARRIER();
		}
		// The last addic. decremented a count of 1: carry set, cr0 equal.
		xer.ca = 1;
		regs.r8.u64 = count;
		cr0.compare<int32_t>(regs.r8.s32, 0, xer);
		regs.r9.u64 = output;
		regs.r10.u64 = index;
		r11.u64 = input;
		f31.f64 = double(phase);
		f0.f64 = double(current);
		regs.f13.f64 = double(previous);
		regs.f12.f64 = double(oneMinusPhase);
		regs.f11.f64 = double(weighted);
		regs.f10.f64 = double(sample);
	}"""


@dataclass
class Group:
    name: str
    output: Path
    # Functions copied, and which of them are inlined into their callers here; calls to all others, hooked ones
    # included, go through sub_X with the context written back.
    functions: list[str]
    inlined: set[str]
    # Any floating-point work (only for code built with SWITCH_EXPLICIT_FMA; see the module comment).
    any_fp: bool = False
    # The FPSCR's cached mode in LocalRegs too.
    local_fpscr: bool = False
    # function -> (first label of the loop, the label right after it, run-time flag macro, the loop's guest
    # instructions, the replacement). Nothing outside the loop may jump into it (checked).
    loop_replacements: dict = field(default_factory=dict)


GROUPS = [
    Group(
        name="audio",
        output=ROOT / "UnleashedRecompLib" / "switch" / "localized_audio.inl",
        # Round 15 (SwitchLocalizedCriHelpers): the reverb's block loop 83146300, which calls the per-sample step
        # 83154100 once per sample; with the step inlined, the registers are loaded and stored once per block instead
        # of once per sample.
        functions=["83154100", "83154050", "83144DC0", "83151FD0", "8316AFE0", "8315B168", "8315AC20", "83146300"],
        inlined={"83154050", "83154100"},
        loop_replacements={
            "8315AC20": ("loc_8315AE14", "loc_8315AE60", "LOCAL_FAST_RESAMPLER", RESAMPLER_LOOP_GUEST, RESAMPLER_LOOP),
        },
    ),
    Group(
        name="main",
        output=ROOT / "UnleashedRecompLib" / "switch" / "localized_main.inl",
        functions=[
            # SwitchNativeSplineAnimation: the Havok spline-compressed animation sampler and its helpers (span search,
            # knot and control-point loading, dequantisation and basis evaluation, blending).
            "82FC4390", "82FC3868", "82FC3180", "82FC3070", "82FC3260", "82FC3990", "82FC4950",
            # SwitchNativePathFollowing: the closest point on a path by iterated projection and its helpers.
            "822D22C8", "822D1F98", "82E84A10", "822D2058", "822D1F40", "82E84A90", "82E861D8", "822DA3C0", "822D2208",
            "822DA1E8", "822DA360", "822DA460", "822DA490", "822DA568",
        ],
        inlined={
            "82FC3868", "82FC3180", "82FC3070", "82FC3260", "82FC3990", "82FC4950",
            "822D1F98", "82E84A10", "822D2058", "822D1F40", "82E84A90", "82E861D8", "822DA3C0", "822D2208", "822DA1E8",
            "822DA360", "822DA460", "822DA490", "822DA568",
        },
        any_fp=True,
        local_fpscr=True,
    ),
    # SwitchNativeMoppVm: Havok's MOPP virtual machines, each recursive, each in a group of its own so that a call (and
    # each recursion, which goes through the hook again) loads and stores only the registers that VM uses: the integer
    # query 82F78148, and the long ray 82F78FB0.
    Group(
        name="mopp query",
        output=ROOT / "UnleashedRecompLib" / "switch" / "localized_mopp_query.inl",
        functions=["82F78148"],
        inlined=set(),
        any_fp=True,
        local_fpscr=True,
    ),
    Group(
        name="mopp ray",
        output=ROOT / "UnleashedRecompLib" / "switch" / "localized_mopp_ray.inl",
        functions=["82F78FB0"],
        inlined=set(),
        any_fp=True,
        local_fpscr=True,
    ),
]


def replace_loop(group: Group, name: str, lines: list[str]) -> list[str]:
    if name not in group.loop_replacements:
        return lines
    first, after, flag, guest, replacement = group.loop_replacements[name]
    start = lines.index(f"{first}:")
    stop = lines.index(f"{after}:")
    loop = lines[start:stop]
    mnemonics = [m.group(1).strip() for m in (re.match(r"^\t// (.*)$", l) for l in loop) if m]
    if mnemonics != guest:
        sys.exit(f"{name}: the loop at {first} is not the one its replacement was written for: {mnemonics}")
    inside = {l[:-1] for l in loop if re.match(r"^loc_[0-9A-F]{8}:$", l)}
    for i, line in enumerate(lines):
        if start <= i < stop:
            continue
        for label in inside:
            if re.search(rf"goto {label};", line):
                sys.exit(f"{name}: {label} is reached from outside the loop; refusing to replace it")
    return (lines[:start] + [f"\tif ({flag})"] + replacement.split("\n") + ["\telse", "\t{"] + loop + ["\t}"] +
            lines[stop:])


def read_bodies() -> dict[str, list[str]]:
    bodies: dict[str, list[str]] = {}
    for path in sorted(PPC_DIR.glob("ppc_recomp.*.cpp")):
        current = None
        for line in path.read_text(encoding="utf-8").splitlines():
            m = DEFINITION.match(line)
            if m:
                current = m.group(1)
                bodies[current] = []
                continue
            if current is None:
                continue
            if line == "}":
                current = None
                continue
            bodies[current].append(line)
    return bodies


def mid_asm_hook_names() -> list[str]:
    return re.findall(r'^name\s*=\s*"(\w+)"', SWA_TOML.read_text(encoding="utf-8"), re.MULTILINE)


def check(group: Group, name: str, body: list[str], hooks: list[str]) -> None:
    for line in body:
        m = re.match(r"^\t// ([a-z][a-z0-9.]*)", line)
        if m:
            mnemonic = m.group(1).rstrip(".")
            if not group.any_fp:
                if mnemonic.startswith("f") and mnemonic not in ALLOWED_FP:
                    sys.exit(f"{name}: '{mnemonic}' is not single-precision floating point; refusing to copy it")
                if mnemonic.startswith("v") and any(k in mnemonic for k in ("fp", "msum", "refp", "rsqrte", "expte", "loge")):
                    sys.exit(f"{name}: vector floating point '{mnemonic}'; refusing to copy it")
            continue
        code = line.split("//")[0]
        if any(hook + "(" in code for hook in hooks):
            sys.exit(f"{name}: calls a mid-asm hook; refusing to copy it")
        if "setjmp" in code or "longjmp" in code:
            sys.exit(f"{name}: setjmp/longjmp; refusing to copy it")
        rest = REGISTER.sub("", CALL.sub("", INDIRECT.sub("", code)))
        rest = rest.replace("ctx.fpscr", "")
        if re.search(r"\bctx\b", rest):
            sys.exit(f"{name}: other use of the context: {line.strip()}")


def transform(group: Group, name: str, body: list[str]) -> list[str]:
    out = []
    for line in body:
        if re.match(r"^\t// ", line):
            out.append(line)
            continue

        def call(m: re.Match) -> str:
            target = m.group(1)
            if target in group.inlined:
                if target == name:
                    sys.exit(f"{name}: calls itself; an inlined function cannot")
                return f"Local_{target}(regs, ctx, base);"
            return f"{{ LOCAL_BEFORE_CALL(); regs.Store(ctx); sub_{target}(ctx, base); regs.Load(ctx); }}"

        def indirect(m: re.Match) -> str:
            return f"{{ LOCAL_BEFORE_CALL(); regs.Store(ctx); PPC_CALL_INDIRECT_FUNC({m.group(1)}); regs.Load(ctx); }}"

        line = REGISTER.sub(r"regs.\1", line)
        if group.local_fpscr:
            line = line.replace("ctx.fpscr", "regs.fpscr")
        line = CALL.sub(call, line)
        line = INDIRECT.sub(indirect, line)
        out.append(line)
    return out


def generate(group: Group, bodies: dict[str, list[str]], hooks: list[str]) -> None:
    registers: set[str] = set()
    for name in group.functions:
        if name not in bodies:
            sys.exit(f"{name}: not in the recompiled sources")
        check(group, name, bodies[name], hooks)
        for line in bodies[name]:
            if not line.startswith("\t//"):
                registers.update(REGISTER.findall(line))

    def order(register: str) -> tuple[int, int]:
        return ("rfv".index(register[0]), int(register[1:]))

    used = sorted(registers, key=order)
    flags = sorted({r[2] for r in group.loop_replacements.values()})
    lines = [
        "// Generated by tools/switch-localize.py from the recompiled sources: functions with their context registers",
        "// in locals (see that script). Do not edit; run it again instead. Included twice by its file, so no include",
        "// guard." + (" The includer defines the flags of the replaced loops: " + ", ".join(flags) + "." if flags else ""),
        "",
        "struct LocalRegs",
        "{",
    ]
    lines += [f"    {'PPCVRegister' if r[0] == 'v' else 'PPCRegister'} {r};" for r in used]
    if group.local_fpscr:
        lines += ["    PPCFPSCRRegister fpscr;"]
    lines += ["", "    void Load(const PPCContext& ctx)", "    {"]
    lines += [f"        {r} = ctx.{r};" for r in used]
    if group.local_fpscr:
        lines += ["        fpscr = ctx.fpscr;"]
    lines += ["    }", "", "    void Store(PPCContext& ctx) const", "    {"]
    lines += [f"        ctx.{r} = {r};" for r in used]
    if group.local_fpscr:
        lines += ["        ctx.fpscr = fpscr;"]
    lines += ["    }", "};", ""]
    # Always inlined into the hook, so that the hook's LocalRegs is a plain local whose address never escapes and
    # GCC keeps its fields in registers (by reference into a separate function it would stay in memory).
    for name in group.functions:
        lines.append(f"static __attribute__((always_inline)) inline void Local_{name}(LocalRegs& regs, PPCContext& ctx, uint8_t* base);")
    lines.append("")
    for name in group.functions:
        lines.append(f"static __attribute__((always_inline)) inline void Local_{name}(LocalRegs& regs, PPCContext& ctx, uint8_t* base)")
        lines.append("{")
        lines += replace_loop(group, name, transform(group, name, bodies[name]))
        lines.append("}")
        lines.append("")

    group.output.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines)
    if group.output.exists() and group.output.read_text(encoding="utf-8") == text:
        print(f"switch-localize: {group.name}: {len(group.functions)} functions, unchanged")
        return
    group.output.write_text(text, encoding="utf-8", newline="\n")
    print(f"switch-localize: {group.name}: {len(group.functions)} functions, registers {', '.join(used)}"
          f"{' + fpscr' if group.local_fpscr else ''} -> {group.output.relative_to(ROOT)}")


def main() -> None:
    bodies = read_bodies()
    hooks = mid_asm_hook_names()
    for group in GROUPS:
        generate(group, bodies, hooks)


if __name__ == "__main__":
    main()
