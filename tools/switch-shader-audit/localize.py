"""localize.py old_dir new_dir hash [--stage ps|vs] [--seed N] [--spec 0x...] [--show N]

Where a translation starts to differ from another (after audit.py reports a difference): runs both on the same inputs,
records every value the statements of main() assign, and prints the first statements of the new translation whose
float values never occur in the old run. The same operations on the same operands give the same float32 bits, so the
first such statement is where the two part ways. Same requirements as audit.py.
"""
import argparse
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audit
import hlsleval as he


def bits(x):
    return struct.unpack('<I', struct.pack('<f', float(x)))[0]


def floats(v):
    return list(v.c) if isinstance(v, he.V) and v.k == 'f' else []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('old_dir')
    ap.add_argument('new_dir')
    ap.add_argument('hash')
    ap.add_argument('--stage', default='ps')
    ap.add_argument('--seed', type=int, default=1037721582)
    ap.add_argument('--spec', type=lambda s: int(s, 0), default=audit.UBO | audit.TEXSIZE)
    ap.add_argument('--show', type=int, default=12)
    args = ap.parse_args()

    audit.PP_DIR = os.path.join(args.new_dir, '..', 'localize.pp')
    os.makedirs(audit.PP_DIR, exist_ok=True)
    name = f'{args.hash}.{args.stage}.hlsl'
    old_text = audit.preprocess(os.path.join(args.old_dir, name))
    new_text = audit.preprocess(os.path.join(args.new_dir, name))
    old, new = he.Program(old_text), he.Program(new_text)
    params = list(new.functions['main'].values())[0][1]
    r = random.Random(args.seed)
    sizes = audit.tex_sizes()
    mem = audit.make_memory(r, sizes, r.getrandbits(16))
    inputs = audit.make_inputs(r, params)

    traces = {}
    for prog, label in ((old, 'old'), (new, 'new')):
        m = he.Machine(prog, mem, he.Textures(sizes, 'smooth', args.seed & 0xFF), he.make_globals(args.spec))
        m.trace = []
        print(label, m.run_main(dict(inputs)))
        traces[label] = m.trace

    old_bits = {bits(x) for _, _, v in traces['old'] for x in floats(v)}
    new_lines = new_text.split('\n')
    main_line = next(i for i, line in enumerate(new_lines, 1) if line.startswith('void main('))
    shown = 0
    for line, _, v in traces['new']:
        if line < main_line:
            continue  # the header's helpers
        missing = [x for x in floats(v) if bits(x) not in old_bits]
        if missing:
            print(f'new line {line}: {new_lines[line - 1].strip()[:220]}')
            print(f'    value {[float(x) for x in floats(v)]}, not in the old run: {[float(x) for x in missing]}')
            shown += 1
            if shown >= args.show:
                break
    if not shown:
        print('every value of the new run also occurs in the old one')


if __name__ == '__main__':
    main()
