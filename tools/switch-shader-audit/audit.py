"""audit.py old_dir new_dir out.txt [--trials N] [--only hash,...] [--workers N] [--skip-done file,...]

Checks one shader translation against another: every shader both folders hold (<hash>.ps|vs.hlsl, as
XENOS_RECOMP_DUMP_DIR writes them; a shader whose .spv is missing is still being translated and is skipped) runs its
main() from both on the same random inputs, constants and textures (hlsleval.py), for the specialization sets the
renderer can use with it. Bits the shader's code never tests are masked away, as the renderer masks the specialization
constants with the shader's specConstantsMask (the quad-sink bit only with a quad variant). Writes one line per shader
and set to out.txt as results come in, then a summary of the sets that differ.

Equal: every output component within 2e-3 relative (NaN equals NaN); a pixel the new translation discards where the
blend would leave the target unchanged (SPEC_CONSTANT_BLEND_SKIP_*); components the specialization declares unused.
Not modelled: levels of detail and derivatives (fetches ignore them, ddx/ddy are 0), helper pixels, filtering.

Needs a Python with numpy, and clang (CLANG, default C:/devkitPro/msys2/clang64/bin/clang.exe) as the preprocessor.
The preprocessed files go to <out>.pp/.
"""
import argparse
import math
import multiprocessing as mp
import os
import random
import re
import struct
import subprocess
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hlsleval as he

CLANG = os.environ.get('CLANG', r'C:/devkitPro/msys2/clang64/bin/clang.exe')
PP_DIR = None

A_TEST = 1 << 1
UBO = 1 << 8
TEXSIZE = 1 << 9
GATHER = 1 << 11
REL_MEM = 1 << 13
BONES_SPEC = 1 << 14
HAS_BONES = 1 << 15
EARLY_OUT = 1 << 20
GATHER_KNOWN = 1 << 21
SINK = 1 << 22
SKIP_ALPHA = 1 << 23
SKIP_ZERO = 1 << 24
QUAD_SINK = 1 << 25

PER_SHADER_BITS = {
    'SINK': 22, 'QUAD_SINK': 25, 'EARLY_OUT': 20, 'SKIP_ALPHA': 23, 'SKIP_ZERO': 24, 'BONES_SPEC': 14, 'HAS_BONES': 15,
    'REL_MEM': 13, 'GATHER': 11, 'GATHER_KNOWN': 21, 'REVERSE_Z': 4, 'A2C': 3, 'BICUBIC': 2, 'R11G11B10': 0,
}

# name, specialization constants, texture model, random unused components
PS_SETS = [
    ('ubo+size', UBO | TEXSIZE, 'smooth', False),
    ('atest', UBO | TEXSIZE | A_TEST, 'smooth', False),
    ('atest+early+sink', UBO | TEXSIZE | A_TEST | EARLY_OUT | SINK, 'smooth', False),
    ('atest+early+quadsink', UBO | TEXSIZE | A_TEST | EARLY_OUT | SINK | QUAD_SINK, 'smooth', False),
    ('skip-alpha+sink', UBO | TEXSIZE | SKIP_ALPHA | EARLY_OUT | SINK, 'smooth', False),
    ('skip-zero', UBO | TEXSIZE | SKIP_ZERO, 'smooth', False),
    ('gather-known', UBO | TEXSIZE | GATHER | GATHER_KNOWN, 'point', False),
    ('gather-runtime', UBO | TEXSIZE | GATHER, 'point', False),
    ('gather+atest+sink', UBO | TEXSIZE | GATHER | GATHER_KNOWN | A_TEST | EARLY_OUT | SINK, 'point', False),
    ('unused-color', UBO | TEXSIZE, 'smooth', True),
    ('bicubic', UBO | TEXSIZE | (1 << 2), 'smooth', False),
    ('r11g11b10', UBO | TEXSIZE | (1 << 0), 'smooth', False),
]
VS_SETS = [
    ('ubo', UBO | TEXSIZE, 'smooth', False),
    ('ubo+relmem', UBO | TEXSIZE | REL_MEM, 'smooth', False),
    ('ubo+revz', UBO | TEXSIZE | (1 << 4), 'smooth', False),
    ('bones', UBO | TEXSIZE | BONES_SPEC | HAS_BONES, 'smooth', False),
    ('bones-off', UBO | TEXSIZE | BONES_SPEC, 'smooth', False),
    ('unused-outputs', UBO | TEXSIZE, 'smooth', True),
    ('r11g11b10', UBO | TEXSIZE | (1 << 0), 'smooth', False),
]


def preprocess(path):
    out = os.path.join(PP_DIR, os.path.basename(os.path.dirname(os.path.abspath(path))) + '.' + os.path.basename(path))
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(path):
        subprocess.run([CLANG, '-E', '-P', '-x', 'c', '-D__spirv__', '-DUNLEASHED_RECOMP', path, '-o', out], check=True,
                       env={'PATH': os.path.dirname(CLANG)})
    with open(out, encoding='utf-8') as f:
        return f.read()


def f32bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def tex_sizes():
    """Power-of-two sizes per 2D slot (the gather rewrites are exact for those)."""
    return {1000 + s: (2 ** (4 + s % 5), 2 ** (5 + (s * 3) % 4)) for s in range(16)}


def make_memory(rng, sizes, gatherable):
    """The vertex, pixel and shared constant blocks (shared: descriptor indices, booleans, texcoord swaps, half-pixel
    offset, alpha threshold, gatherable slots, 2D texture sizes), as the renderer lays them out."""
    pixel = [f32bits(rng.uniform(-1.5, 1.5)) for _ in range(224 * 4)]
    vertex = [f32bits(rng.uniform(-1.5, 1.5)) for _ in range(256 * 4)]
    shared = [0] * (26 * 4)
    for s in range(16):
        shared[s] = 1000 + s
        shared[16 + s] = 3000 + s
        shared[32 + s] = 2000 + s
        shared[48 + s] = 4000 + s
    shared[64] = rng.getrandbits(32)  # g_Booleans
    shared[65] = rng.getrandbits(16)  # g_SwappedTexcoords
    shared[66] = f32bits(rng.uniform(-0.002, 0.002))  # g_HalfPixelOffset
    shared[67] = f32bits(rng.uniform(-0.002, 0.002))
    shared[68] = f32bits(rng.uniform(0.0, 1.0))  # g_AlphaThreshold
    shared[69] = gatherable  # g_GatherableSlots
    for s in range(16):
        w, h = sizes[1000 + s]
        shared[72 + 2 * s] = f32bits(float(w))
        shared[72 + 2 * s + 1] = f32bits(float(h))
    return he.Memory({'pixel': pixel, 'vertex': vertex, 'shared': shared})


def make_inputs(rng, params):
    inputs = {}
    for mode, typ, name, sem in params:
        if mode != 'in':
            continue
        semu = (sem or '').upper()
        ti = he.type_info(typ)
        if semu == 'SV_POSITION':
            inputs[name] = he.V('f', [he.F32(rng.uniform(0, 1280)), he.F32(rng.uniform(0, 720)), he.F32(rng.uniform(0, 1)),
                                      he.F32(rng.uniform(0.1, 2))])
        elif semu == 'SV_ISFRONTFACE':
            inputs[name] = he.V(ti[0], [he.conv(rng.random() < 0.5, ti[0])])
        elif semu.startswith('BLENDINDICES'):
            inputs[name] = he.V(ti[0], [he.conv(rng.randrange(0, 24), ti[0]) for _ in range(ti[1])])
        elif semu.startswith('COLOR') or semu.startswith('BLENDWEIGHT'):
            inputs[name] = he.V(ti[0], [he.conv(rng.uniform(0, 1), ti[0]) for _ in range(ti[1])])
        elif semu.startswith('SV_VERTEXID') or semu.startswith('SV_INSTANCEID'):
            inputs[name] = he.V('u', [rng.randrange(0, 1000)])
        elif ti[0] == 'f':
            inputs[name] = he.V('f', [he.F32(rng.uniform(-1.2, 1.2)) for _ in range(ti[1])])
        else:
            inputs[name] = he.V(ti[0], [he.conv(rng.getrandbits(32), ti[0]) for _ in range(ti[1])])
    return inputs


def close(a, b):
    a, b = float(a), float(b)
    if math.isnan(a) and math.isnan(b):
        return True
    if math.isinf(a) or math.isinf(b):
        return a == b
    return abs(a - b) <= 2e-3 * max(1.0, abs(a), abs(b))


def blend_skip_ok(spec, out):
    """A pixel the new translation may discard because the blend would leave it unchanged."""
    c = [float(x) for x in out['oC0'].c]
    if any(math.isnan(x) for x in c):
        return False
    return bool((spec & SKIP_ALPHA and c[3] <= 0.0) or (spec & SKIP_ZERO and all(x <= 0.0 for x in c)))


def compare(old_out, new_out, spec=0):
    """None if equal, else the first difference."""
    if old_out is None or new_out is None:
        if old_out is None and new_out is None:
            return None
        if new_out is None and blend_skip_ok(spec, old_out):
            return None
        return f'discard: old {"discarded" if old_out is None else "kept"}, new {"discarded" if new_out is None else "kept"}'
    for name in old_out:
        if name not in new_out:
            continue
        a, b = old_out[name], new_out[name]
        if isinstance(a, he.V):
            for c, (x, y) in enumerate(zip(a.c, b.c)):
                if not close(x, y):
                    return f'{name}.{"xyzw"[c]}: old {float(x):.6g} new {float(y):.6g}'
    return None


def main_text(text):
    i = text.find('\nvoid main(')
    return text[i:] if i >= 0 else text


def shader_mask(new_main):
    """The per-shader bits the renderer could leave set: those the shader's code tests."""
    mask = 0xFFFFFFFF
    for bit in PER_SHADER_BITS.values():
        if f'(1 << {bit})' not in new_main:
            mask &= ~(1 << bit)
    # The quad variant's bit is in the mask only when the shader has one (its condition can name the bit without).
    if 'QuadReadAcrossX' not in new_main:
        mask &= ~QUAD_SINK
    return mask


SEMANTIC_INDEX = re.compile(r'(TEXCOORD|COLOR)(\d+)$', re.I)


def unused_slot(sem):
    m = SEMANTIC_INDEX.match(sem or '')
    if not m:
        return None
    k = int(m.group(2))
    return k if m.group(1).upper() == 'TEXCOORD' else 16 + k


def run_shader(job):
    h, stage, old_path, new_path, trials, pp_dir = job
    global PP_DIR
    PP_DIR = pp_dir
    out = []
    try:
        old_text = preprocess(old_path)
        new_text = preprocess(new_path)
        old = he.Program(old_text)
        new = he.Program(new_text)
    except Exception as e:  # noqa
        return h, stage, [('parse', 0, 0, f'{type(e).__name__}: {e}', '')]
    mask = shader_mask(main_text(new_text))
    params = list(new.functions['main'].values())[0][1]
    out_params = [(name, sem) for mode, typ, name, sem in params if mode == 'out']
    seen = set()
    for name, spec, texmode, unused in (PS_SETS if stage == 'ps' else VS_SETS):
        eff = (spec & mask) | (spec & (UBO | TEXSIZE))
        if (eff, texmode, unused) in seen:
            continue
        seen.add((eff, texmode, unused))
        rng = random.Random(zlib.crc32(f'{h}{name}'.encode()))
        diffs = ran = 0
        first = errors = ''
        for _ in range(trials):
            seed = rng.getrandbits(30)
            r = random.Random(seed)
            sizes = tex_sizes()
            mem = make_memory(r, sizes, 0xFFFF if (eff & GATHER) else r.getrandbits(16))
            if eff & BONES_SPEC:  # the renderer specializes on mrgHasBone (bit 0 of g_Booleans) as it is
                mem.blocks['shared'][64] = (mem.blocks['shared'][64] & ~1) | (1 if eff & HAS_BONES else 0)
            inputs = make_inputs(r, params)
            unused_bits = (0, 0, 0)
            colour_mask = 0
            spec_new = eff
            if unused and stage == 'vs':
                unused_bits = (r.getrandbits(32), r.getrandbits(32), r.getrandbits(8))
            if unused and stage == 'ps':
                colour_mask = r.getrandbits(3)  # x, y, z only (alpha feeds the tests)
                spec_new |= colour_mask << 16
            res = []
            for prog, label in ((old, 'old'), (new, 'new')):
                g = he.make_globals(spec_new if label == 'new' else eff, unused_bits if label == 'new' else (0, 0, 0))
                m = he.Machine(prog, mem, he.Textures(sizes, texmode, seed & 0xFF), g)
                try:
                    res.append(m.run_main(dict(inputs)))
                except he.EvalError as e:
                    errors = errors or f'{label}: {e}'
                    res.append('error')
                except RecursionError:
                    errors = errors or f'{label}: recursion'
                    res.append('error')
            if 'error' in res:
                continue
            ran += 1
            a, b = res
            if unused and a is not None and b is not None:
                if stage == 'ps':
                    for c in range(3):
                        if colour_mask & (1 << c):
                            a['oC0'].c[c] = b['oC0'].c[c]
                else:
                    bits = unused_bits[0] | (unused_bits[1] << 32) | (unused_bits[2] << 64)
                    for pname, sem in out_params:
                        k = unused_slot(sem)
                        if k is None or pname not in a:
                            continue
                        for c in range(4):
                            if bits & (1 << (4 * k + c)):
                                a[pname].c[c] = b[pname].c[c]
            d = compare(a, b, spec_new)
            if d:
                diffs += 1
                first = first or f'seed {seed}: {d}'
        out.append((name, diffs, ran, errors, first))
    return h, stage, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('old_dir')
    ap.add_argument('new_dir')
    ap.add_argument('out')
    ap.add_argument('--trials', type=int, default=16)
    ap.add_argument('--only', default=None)
    ap.add_argument('--workers', type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument('--skip-done', default=None, help='comma-separated earlier result files whose shaders to skip')
    args = ap.parse_args()

    pp_dir = args.out + '.pp'
    os.makedirs(pp_dir, exist_ok=True)
    done = set()
    for prev in (args.skip_done or '').split(','):
        if prev and os.path.exists(prev):
            for line in open(prev, encoding='utf-8'):
                m = re.match(r'([0-9A-F]{16}\.(?:ps|vs)) ', line)
                if m:
                    done.add(m.group(1))
    jobs = []
    for f in sorted(os.listdir(args.new_dir)):
        m = re.match(r'([0-9A-F]{16})\.(ps|vs)\.hlsl$', f)
        if not m:
            continue
        h, stage = m.groups()
        if (args.only and h not in args.only.split(',')) or f'{h}.{stage}' in done:
            continue
        old_path = os.path.join(args.old_dir, f)
        if not os.path.exists(old_path) or not os.path.exists(os.path.join(args.new_dir, f'{h}.{stage}.spv')):
            continue
        jobs.append((h, stage, old_path, os.path.join(args.new_dir, f), args.trials, pp_dir))
    print(f'{len(jobs)} shaders', flush=True)

    start = time.time()
    bad, errs = [], []
    with open(args.out, 'w', encoding='utf-8') as out, mp.Pool(args.workers) as pool:
        for n, (h, stage, results) in enumerate(pool.imap_unordered(run_shader, jobs), 1):
            for name, diffs, ran, errors, first in results:
                out.write(f'{h}.{stage} {name}: {diffs}/{ran} differ{"  ERR " + errors if errors else ""}'
                          f'{"  FIRST " + first if first else ""}\n')
                if diffs:
                    bad.append((h, stage, name, diffs, ran, first))
                if errors:
                    errs.append((h, stage, name, errors))
            out.flush()
            if n % 50 == 0:
                print(f'{n}/{len(jobs)} done, {time.time() - start:.0f} s, {len(bad)} differing sets, {len(errs)} with errors',
                      flush=True)
        out.write('\n==== SUMMARY\n')
        for h, stage, name, diffs, ran, first in bad:
            out.write(f'DIFF {h}.{stage} {name}: {diffs}/{ran}  {first}\n')
        for h, stage, name, e in errs:
            out.write(f'ERR {h}.{stage} {name}: {e}\n')
    print(f'done in {time.time() - start:.0f} s: {len(bad)} differing sets, {len(errs)} with errors')


if __name__ == '__main__':
    main()
