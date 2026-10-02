"""hlsleval.py: an interpreter for XenosRecomp's HLSL translations, for audit.py and localize.py.

Evaluates main() of a translation after the C preprocessor (clang -E -P -x c -D__spirv__ -DUNLEASHED_RECOMP) on chosen
inputs, in float32. The header's own helper functions are interpreted too; only the resources are modelled: memory
blocks for the constants (the push-constant pointers and the UBOs read the same bytes), textures as smooth functions of
the coordinate (or point-sampled texel grids, for the gather paths), samplers as indices. Levels of detail and
derivatives are not modelled (fetches ignore them, ddx/ddy are 0); a quad is four copies of one pixel.

Values are V(kind, comps): kind in 'f' (numpy float32), 'i' (int32), 'u' (uint32), 'b' (bool); a list of 1-4 components.
Structs are dicts, arrays and matrices lists, resources Res objects. Machine.trace, when a list, receives every
assignment of main() as (line of the preprocessed text, target, value).
"""
import math
import re
import struct

import numpy as np

np.seterr(all='ignore')
F32 = np.float32


class EvalError(Exception):
    pass


class Discard(Exception):
    pass


class Return(Exception):
    def __init__(self, value):
        self.value = value


# ------------------------------------------------------------------------------------------------- tokenizer
TOKEN_RE = re.compile(r'''
    (?P<ws>\s+)
  | (?P<num>0[xX][0-9a-fA-F]+[uUlL]*|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fFhHuUlL]*)
  | (?P<id>[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)
  | (?P<op><<=|>>=|\+\+|--|&&|\|\||==|!=|<=|>=|\+=|-=|\*=|/=|%=|\|=|&=|\^=|<<|>>|[-+*/%<>=!~&|^?:;,.()\[\]{}])
''', re.X)


TOKEN_LINES = {}


def tokenize(text):
    out = []
    lines = []
    pos = 0
    line = 1
    while pos < len(text):
        m = TOKEN_RE.match(text, pos)
        if not m:
            raise EvalError(f'cannot tokenize at {text[pos:pos + 40]!r}')
        if m.lastgroup == 'ws':
            line += m.group().count('\n')
            pos = m.end()
            continue
        pos = m.end()
        out.append((m.lastgroup, m.group()))
        lines.append(line)
    TOKEN_LINES[id(out)] = lines
    return out


SCALAR_TYPES = {'float': 'f', 'half': 'f', 'double': 'f', 'int': 'i', 'uint': 'u', 'bool': 'b', 'dword': 'u',
                'uint64_t': 'u', 'min16float': 'f'}


def mat_info(name):
    """(rows, cols) for floatRxC matrix type names, else None."""
    m = re.fullmatch(r'(?:float|half|min16float)([1-4])x([1-4])', name)
    return (int(m.group(1)), int(m.group(2))) if m else None


class Mat(list):
    """A matrix: a list of row vectors (V)."""


def type_info(name):
    """(kind, size) for scalar/vector type names, else None."""
    m = re.fullmatch(r'(float|half|int|uint|bool|double|min16float)([1-4])?', name)
    if m:
        return SCALAR_TYPES[m.group(1)], int(m.group(2) or 1)
    if name in SCALAR_TYPES:
        return SCALAR_TYPES[name], 1
    return None


# ------------------------------------------------------------------------------------------------- parser
class Parser:
    def __init__(self, tokens, structs):
        self.t = tokens
        self.i = 0
        self.structs = structs
        self.lines = TOKEN_LINES.get(id(tokens))

    def line(self):
        return self.lines[self.i] if self.lines and self.i < len(self.lines) else 0

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else ('eof', '')

    def next(self):
        tok = self.peek()
        self.i += 1
        return tok

    def accept(self, value):
        if self.peek()[1] == value:
            self.i += 1
            return True
        return False

    def expect(self, value):
        tok = self.next()
        if tok[1] != value:
            raise EvalError(f'expected {value!r}, got {tok[1]!r} near {self.context()}')
        return tok

    def context(self):
        return ' '.join(v for _, v in self.t[max(0, self.i - 8):self.i + 8])

    def is_type(self, k=0):
        tok = self.peek(k)
        if tok[0] != 'id':
            return False
        return type_info(tok[1]) is not None or mat_info(tok[1]) is not None or tok[1] in self.structs or tok[1] in (
            'Texture2D', 'Texture3D', 'TextureCube', 'SamplerState', 'SamplerComparisonState')

    def parse_type(self):
        name = self.next()[1]
        if self.peek()[1] == '<':  # Texture2D<float4>
            depth = 0
            while True:
                v = self.next()[1]
                if v == '<':
                    depth += 1
                elif v == '>':
                    depth -= 1
                    if depth == 0:
                        break
        return name

    # statements
    def skip_attributes(self):
        while self.peek()[1] == '[' and self.peek(1)[0] == 'id' and self.peek(1)[1] in (
                'branch', 'flatten', 'unroll', 'loop', 'fastopt', 'allow_uav_condition', 'forcecase', 'call'):
            self.next()
            depth = 1
            while depth:
                v = self.next()[1]
                if v == '[':
                    depth += 1
                elif v == ']':
                    depth -= 1

    def statement(self):
        self.skip_attributes()
        tok = self.peek()
        v = tok[1]
        if v == '{':
            self.next()
            body = []
            while not self.accept('}'):
                body.append(self.statement())
            return ('block', body)
        if v == ';':
            self.next()
            return ('block', [])
        if v == 'if':
            self.next()
            self.expect('(')
            cond = self.expression()
            self.expect(')')
            then = self.statement()
            other = None
            if self.accept('else'):
                other = self.statement()
            return ('if', cond, then, other)
        if v == 'for':
            self.next()
            self.expect('(')
            init = None if self.peek()[1] == ';' else (self.declaration() if self.is_type() else ('expr', self.expression()))
            if init is None or init[0] == 'expr':
                self.expect(';')
            cond = None if self.peek()[1] == ';' else self.expression()
            self.expect(';')
            step = None if self.peek()[1] == ')' else self.expression()
            self.expect(')')
            body = self.statement()
            return ('for', init, cond, step, body)
        if v == 'while':
            self.next()
            self.expect('(')
            cond = self.expression()
            self.expect(')')
            return ('for', None, cond, None, self.statement())
        if v == 'do':
            self.next()
            body = self.statement()
            self.expect('while')
            self.expect('(')
            cond = self.expression()
            self.expect(')')
            self.expect(';')
            return ('do', body, cond)
        if v == 'switch':
            self.next()
            self.expect('(')
            value = self.expression()
            self.expect(')')
            self.expect('{')
            items = []  # ('case', expr) / ('default',) / statement
            while not self.accept('}'):
                self.skip_attributes()
                if self.peek()[1] == 'case':
                    self.next()
                    label = self.ternary()
                    self.expect(':')
                    items.append(('case', label))
                elif self.peek()[1] == 'default':
                    self.next()
                    self.expect(':')
                    items.append(('default',))
                else:
                    items.append(self.statement())
            return ('switch', value, items)
        if v == 'return':
            self.next()
            value = None if self.peek()[1] == ';' else self.expression()
            self.expect(';')
            return ('return', value)
        if v == 'discard':
            self.next()
            self.expect(';')
            return ('discard',)
        if v == 'break':
            self.next()
            self.expect(';')
            return ('break',)
        if v == 'continue':
            self.next()
            self.expect(';')
            return ('continue',)
        line = self.line()
        if v in ('const', 'static', 'precise', 'uniform') or self.is_type():
            d = self.declaration()
            return d + (line,)
        e = self.expression()
        self.expect(';')
        return ('expr', e, line)

    def declaration(self):
        while self.peek()[1] in ('const', 'static', 'precise', 'uniform'):
            self.next()
        typ = self.parse_type()
        decls = []
        while True:
            name = self.next()[1]
            array = None
            if self.accept('['):
                array = self.expression()
                self.expect(']')
            init = None
            if self.accept('='):
                init = self.assignment()
            decls.append((name, array, init))
            if not self.accept(','):
                break
        self.expect(';')
        return ('decl', typ, decls)

    # expressions
    def expression(self):
        e = self.assignment()
        while self.peek()[1] == ',':
            self.next()
            e = ('comma', e, self.assignment())
        return e

    def assignment(self):
        lhs = self.ternary()
        op = self.peek()[1]
        if op in ('=', '+=', '-=', '*=', '/=', '%=', '|=', '&=', '^=', '<<=', '>>='):
            self.next()
            rhs = self.assignment()
            return ('assign', op, lhs, rhs)
        return lhs

    def ternary(self):
        cond = self.binary(0)
        if self.accept('?'):
            a = self.assignment()
            self.expect(':')
            b = self.assignment()
            return ('ternary', cond, a, b)
        return cond

    LEVELS = [('||',), ('&&',), ('|',), ('^',), ('&',), ('==', '!='), ('<', '<=', '>', '>='), ('<<', '>>'),
              ('+', '-'), ('*', '/', '%')]

    def binary(self, level):
        if level == len(self.LEVELS):
            return self.unary()
        e = self.binary(level + 1)
        while self.peek()[1] in self.LEVELS[level] and self.peek()[0] == 'op':
            op = self.next()[1]
            e = ('bin', op, e, self.binary(level + 1))
        return e

    def unary(self):
        v = self.peek()[1]
        if v in ('-', '+', '!', '~') and self.peek()[0] == 'op':
            self.next()
            return ('un', v, self.unary())
        if v in ('++', '--'):
            self.next()
            return ('preinc', v, self.unary())
        if v == '(' and self.is_type(1) and self.peek(2)[1] == ')':
            self.next()
            typ = self.parse_type()
            self.expect(')')
            return ('cast', typ, self.unary())
        return self.postfix(self.primary())

    def postfix(self, e):
        while True:
            v = self.peek()[1]
            if v == '(':
                self.next()
                args = []
                if not self.accept(')'):
                    while True:
                        args.append(self.assignment())
                        if self.accept(')'):
                            break
                        self.expect(',')
                e = ('call', e, args)
            elif v == '[':
                self.next()
                idx = self.expression()
                self.expect(']')
                e = ('index', e, idx)
            elif v == '.':
                self.next()
                e = ('member', e, self.next()[1])
            elif v in ('++', '--'):
                self.next()
                e = ('postinc', v, e)
            else:
                return e

    def primary(self):
        kind, v = self.next()
        if kind == 'num':
            return ('num', v)
        if kind == 'id':
            if v in ('true', 'false'):
                return ('bool', v == 'true')
            if v == 'vk::RawBufferLoad' and self.peek()[1] == '<':
                self.next()
                typ = self.next()[1]
                self.expect('>')
                return ('id', 'vk::RawBufferLoad<' + typ + '>')
            return ('id', v)
        if v == '(':
            e = self.expression()
            self.expect(')')
            return e
        raise EvalError(f'unexpected {v!r} near {self.context()}')


# ------------------------------------------------------------------------------------------------- program
class Program:
    """Top level of a preprocessed HLSL file: structs and functions (by name and parameter count)."""

    def __init__(self, text):
        self.tokens = tokenize(text)
        self.structs = {}
        self.functions = {}
        self.scan()

    def scan(self):
        t = self.tokens
        i = 0
        n = len(t)
        while i < n:
            v = t[i][1]
            if v == 'struct' and i + 2 < n and t[i + 2][1] == '{':
                name = t[i + 1][1]
                j = i + 3
                fields = []
                p = Parser(t, self.structs)
                p.i = j
                while p.peek()[1] != '}':
                    typ = p.parse_type()
                    fname = p.next()[1]
                    arr = None
                    if p.accept('['):
                        arr = int(p.next()[1].rstrip('uU'))
                        p.expect(']')
                    p.expect(';')
                    fields.append((fname, typ, arr))
                p.expect('}')
                p.accept(';')
                self.structs[name] = fields
                i = p.i
                continue
            # function definition: [attr] type name ( params ) {
            if t[i][0] == 'id' and i + 2 < n and t[i + 1][0] == 'id' and t[i + 2][1] == '(':
                # find the matching ')' and check for '{'
                depth = 0
                j = i + 2
                while j < n:
                    if t[j][1] == '(':
                        depth += 1
                    elif t[j][1] == ')':
                        depth -= 1
                        if depth == 0:
                            break
                    j += 1
                if j + 1 < n and t[j + 1][1] == '{':
                    name = t[i + 1][1]
                    params = self.parse_params(t[i + 3:j])
                    p = Parser(t, self.structs)
                    p.i = j + 1
                    body = p.statement()
                    self.functions.setdefault(name, {})[len(params)] = (t[i][1], params, body)
                    i = p.i
                    continue
            # anything else: skip to the end of the declaration (';' at depth 0, or a block)
            depth = 0
            while i < n:
                v = t[i][1]
                if v in ('(', '[', '{'):
                    depth += 1
                elif v in (')', ']', '}'):
                    depth -= 1
                    if depth == 0 and v == '}' and i + 1 < n and t[i + 1][1] != ';':
                        i += 1
                        break
                elif v == ';' and depth == 0:
                    i += 1
                    break
                i += 1

    def parse_params(self, toks):
        params = []
        cur = []
        depth = 0
        for tok in toks + [('op', ',')]:
            if tok[1] == ',' and depth == 0:
                if cur:
                    params.append(cur)
                cur = []
                continue
            if tok[1] in '(<':
                depth += 1
            elif tok[1] in ')>':
                depth -= 1
            cur.append(tok)
        out = []
        for p in params:
            words = [v for _, v in p]
            while words and words[0] == '[':
                depth = 0
                while words:
                    w = words.pop(0)
                    if w == '[':
                        depth += 1
                    elif w == ']':
                        depth -= 1
                        if depth == 0:
                            break
            if ':' in words:  # semantic
                sem = words[words.index(':') + 1]
                words = words[:words.index(':')]
            else:
                sem = None
            mode = 'in'
            while words and words[0] in ('in', 'out', 'inout', 'const', 'uniform', 'precise', 'nointerpolation',
                                         'linear', 'centroid', 'noperspective', 'sample'):
                if words[0] in ('out', 'inout'):
                    mode = words[0]
                words = words[1:]
            # type may be Texture2D < float4 >
            name = words[-1]
            typ = words[0]
            out.append((mode, typ, name, sem))
        return out


# ------------------------------------------------------------------------------------------------- values
class V:
    __slots__ = ('k', 'c')

    def __init__(self, k, c):
        self.k = k
        self.c = c

    def __repr__(self):
        return f'V({self.k}, {[float(x) if self.k == "f" else x for x in self.c]})'


def conv(x, k):
    """Converts one component to kind k."""
    if k == 'f':
        if isinstance(x, (bool, np.bool_)):
            return F32(1.0 if x else 0.0)
        return F32(x)
    if k == 'b':
        return bool(x != 0) if not isinstance(x, (np.floating, float)) else bool(x != 0.0)
    if isinstance(x, (np.floating, float)):
        if math.isnan(x):
            v = 0
        elif math.isinf(x):
            v = (2 ** 31 - 1) if (x > 0) else -(2 ** 31)
            if k == 'u':
                v = 0xFFFFFFFF if x > 0 else 0
        else:
            v = int(x)  # truncation
        if k == 'u':
            return v & 0xFFFFFFFF if v >= 0 else (0 if v < 0 and False else v & 0xFFFFFFFF)
        return wrap_i(v)
    v = int(x)
    return v & 0xFFFFFFFF if k == 'u' else wrap_i(v)


def wrap_i(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def cast(val, k, size=None):
    if not isinstance(val, V):
        raise EvalError(f'cannot convert {val!r} to {k}')
    comps = val.c
    if size is not None:
        if len(comps) == 1 and size > 1:
            comps = comps * size
        elif len(comps) > size:
            comps = comps[:size]
        elif len(comps) < size:
            raise EvalError(f'cannot widen {val} to {size}')
    return V(k, [conv(x, k) for x in comps])


def zero_of(typ, structs, array=None):
    if array is not None:
        return [zero_of(typ, structs) for _ in range(array)]
    ti = type_info(typ)
    if ti:
        k, n = ti
        return V(k, [conv(0, k)] * n)
    mi = mat_info(typ)
    if mi:
        return Mat(V('f', [F32(0.0)] * mi[1]) for _ in range(mi[0]))
    if typ in structs:
        return {f: zero_of(ft, structs, fa) for f, ft, fa in structs[typ]}
    return None  # resources


def promote(a, b):
    order = {'b': 0, 'i': 1, 'u': 2, 'f': 3}
    return a if order[a] >= order[b] else b


def broadcast(a, b):
    la, lb = len(a.c), len(b.c)
    if la == lb:
        return a.c, b.c
    if la == 1:
        return a.c * lb, b.c
    if lb == 1:
        return a.c, b.c * la
    n = min(la, lb)
    return a.c[:n], b.c[:n]


SWZ = {'x': 0, 'y': 1, 'z': 2, 'w': 3, 'r': 0, 'g': 1, 'b': 2, 'a': 3}


def is_swizzle(name):
    return len(name) <= 4 and all(ch in SWZ for ch in name) and (
        all(ch in 'xyzw' for ch in name) or all(ch in 'rgba' for ch in name))


# ------------------------------------------------------------------------------------------------- resources
class Res:
    def __init__(self, kind, index):
        self.kind = kind  # 'tex2d', 'tex3d', 'texcube', 'sampler', 'ptr', 'ubo', 'heap'
        self.index = index

    def __repr__(self):
        return f'Res({self.kind}, {self.index})'


class Memory:
    """Constant blocks as 32-bit words; the push-constant pointers and the UBOs read the same words."""

    def __init__(self, blocks):
        self.blocks = blocks  # name -> list of uint32 words

    def word(self, block, byte):
        words = self.blocks[block]
        if byte % 4:
            raise EvalError(f'unaligned load {block}+{byte}')
        i = byte // 4
        return words[i] if 0 <= i < len(words) else 0

    def load(self, block, byte, typ):
        k, n = type_info(typ)
        comps = []
        for c in range(n):
            w = self.word(block, byte + 4 * c)
            if k == 'f':
                comps.append(F32(struct.unpack('<f', struct.pack('<I', w))[0]))
            elif k == 'u':
                comps.append(w)
            elif k == 'i':
                comps.append(wrap_i(w))
            else:
                comps.append(w != 0)
        return V(k, comps)


class Textures:
    """Texture contents: 'smooth' (a continuous function of the coordinate per texture) or 'point' (texel grids)."""

    def __init__(self, sizes, mode='smooth', seed=0):
        self.sizes = sizes  # index -> (w, h)
        self.mode = mode
        self.seed = seed

    def size(self, index):
        return self.sizes.get(index, (64, 64))

    def texel(self, index, i, j, ch):
        h = (index * 73856093 ^ i * 19349663 ^ j * 83492791 ^ ch * 2654435761 ^ self.seed * 97) & 0xFFFFFFFF
        h = (h ^ (h >> 13)) * 0x5bd1e995 & 0xFFFFFFFF
        return ((h ^ (h >> 15)) & 0xFFFF) / 65535.0

    def sample2d(self, index, u, v):
        if self.mode == 'point':
            w, h = self.size(index)
            i, j = math.floor(u * w) % w, math.floor(v * h) % h
            return [F32(self.texel(index, i, j, ch)) for ch in range(4)]
        out = []
        for ch in range(4):
            a = 1.3 + 0.37 * ((index * 7 + ch * 3 + self.seed) % 11)
            b = 0.7 + 0.29 * ((index * 5 + ch * 11 + self.seed) % 13)
            out.append(F32(0.5 + 0.45 * math.sin(a * u + 0.3 * ch + index * 0.1) * math.cos(b * v - 0.2 * ch)))
        return out

    def gather_red(self, index, u, v):
        w, h = self.size(index)
        i0 = math.floor(u * w - 0.5)
        j0 = math.floor(v * h - 0.5)
        t = lambda i, j: F32(self.texel(index, i % w, j % h, 0))
        # x = (i0, j0 + 1), y = (i0 + 1, j0 + 1), z = (i0 + 1, j0), w = (i0, j0)
        return [t(i0, j0 + 1), t(i0 + 1, j0 + 1), t(i0 + 1, j0), t(i0, j0)]

    def sample_cube(self, index, x, y, z):
        n = math.sqrt(x * x + y * y + z * z)
        if n == 0 or not math.isfinite(n):
            x, y, z = 0.0, 0.0, 1.0
        else:
            x, y, z = x / n, y / n, z / n
        return [F32(0.5 + 0.45 * math.sin(1.7 * x + 0.9 * y * (ch + 1) - 1.1 * z + index * 0.13 + ch)) for ch in range(4)]

    def sample3d(self, index, u, v, w):
        return [F32(0.5 + 0.45 * math.sin(1.1 * u + 1.9 * v + 0.7 * w + ch + index * 0.17)) for ch in range(4)]


def fval(v):
    return float(v)


# ------------------------------------------------------------------------------------------------- evaluator
class Machine:
    def __init__(self, program, memory, textures, globals_):
        self.p = program
        self.mem = memory
        self.tex = textures
        self.globals = globals_  # name -> value (spec constants etc.)
        self.steps = 0
        self.trace = None  # list of (line, name, value) of every assignment statement, when set

    # ---- variables
    def run_main(self, inputs, entry='main'):
        fns = self.p.functions.get(entry)
        if not fns:
            raise EvalError('no main')
        rtype, params, body = list(fns.values())[0]
        scope = {}
        outs = []
        for mode, typ, name, sem in params:
            if mode == 'in':
                if name not in inputs:
                    raise EvalError(f'missing input {name} ({typ} : {sem})')
                ti = type_info(typ)
                scope[name] = cast(inputs[name], ti[0], ti[1]) if ti else inputs[name]
            else:
                scope[name] = zero_of(typ, self.p.structs)
                outs.append(name)
        try:
            self.exec(body, [scope])
        except Return:
            pass
        except Discard:
            return None
        return {name: scope[name] for name in outs}

    def lookup(self, scopes, name):
        for s in reversed(scopes):
            if name in s:
                return s
        return None

    # ---- statements
    def exec(self, st, scopes):
        kind = st[0]
        if kind == 'block':
            scopes.append({})
            try:
                for s in st[1]:
                    self.exec(s, scopes)
            finally:
                scopes.pop()
        elif kind == 'expr':
            v = self.eval(st[1], scopes)
            if self.trace is not None and st[1][0] == 'assign' and len(scopes) <= 64:
                self.trace.append((st[2] if len(st) > 2 else 0, st[1][2], v))
        elif kind == 'decl':
            typ = st[1]
            for name, array, init in st[2]:
                arr = None
                if array is not None:
                    arr = int(self.scalar(self.eval(array, scopes)))
                if init is not None:
                    val = self.eval(init, scopes)
                    ti = type_info(typ)
                    if ti and isinstance(val, V):
                        val = cast(val, ti[0], ti[1])
                    elif isinstance(val, (dict, Mat)):
                        val = deepcopy(val)
                    scopes[-1][name] = val
                    if self.trace is not None:
                        self.trace.append((st[3] if len(st) > 3 else 0, ('id', name), val))
                else:
                    scopes[-1][name] = zero_of(typ, self.p.structs, arr)
        elif kind == 'if':
            if self.truth(self.eval(st[1], scopes)):
                self.exec(st[2], scopes)
            elif st[3] is not None:
                self.exec(st[3], scopes)
        elif kind == 'for':
            scopes.append({})
            try:
                init, cond, step, body = st[1:]
                if init is not None:
                    self.exec(init, scopes)
                count = 0
                while cond is None or self.truth(self.eval(cond, scopes)):
                    count += 1
                    if count > 10000:
                        raise EvalError('loop runaway')
                    try:
                        self.exec(body, scopes)
                    except LoopBreak:
                        break
                    except LoopContinue:
                        pass
                    if step is not None:
                        self.eval(step, scopes)
            finally:
                scopes.pop()
        elif kind == 'do':
            scopes.append({})
            try:
                count = 0
                while True:
                    count += 1
                    if count > 10000:
                        raise EvalError('loop runaway')
                    try:
                        self.exec(st[1], scopes)
                    except LoopBreak:
                        break
                    except LoopContinue:
                        pass
                    if not self.truth(self.eval(st[2], scopes)):
                        break
            finally:
                scopes.pop()
        elif kind == 'switch':
            value = self.scalar(self.eval(st[1], scopes))
            items = st[2]
            start = None
            default = None
            for i, item in enumerate(items):
                if item[0] == 'case' and start is None:
                    if int(self.scalar(self.eval(item[1], scopes))) == int(value):
                        start = i
                elif item[0] == 'default':
                    default = i
            if start is None:
                start = default
            if start is not None:
                scopes.append({})
                try:
                    for item in items[start:]:
                        if item[0] in ('case', 'default'):
                            continue
                        self.exec(item, scopes)
                except LoopBreak:
                    pass
                finally:
                    scopes.pop()
        elif kind == 'return':
            raise Return(None if st[1] is None else self.eval(st[1], scopes))
        elif kind == 'discard':
            raise Discard()
        elif kind == 'break':
            raise LoopBreak()
        elif kind == 'continue':
            raise LoopContinue()
        else:
            raise EvalError(f'statement {kind}')

    def truth(self, v):
        if isinstance(v, V):
            if len(v.c) != 1:
                raise EvalError(f'vector condition {v}')
            return bool(v.c[0])
        raise EvalError(f'condition {v!r}')

    def scalar(self, v):
        if not isinstance(v, V) or len(v.c) != 1:
            raise EvalError(f'not a scalar: {v!r}')
        return v.c[0]

    # ---- lvalues
    def store(self, target, value, scopes):
        kind = target[0]
        if kind == 'id':
            s = self.lookup(scopes, target[1])
            if s is None:
                raise EvalError(f'assignment to unknown {target[1]}')
            old = s[target[1]]
            if isinstance(old, V) and isinstance(value, V):
                value = cast(value, old.k, len(old.c))
            s[target[1]] = value
        elif kind == 'member':
            base = self.eval(target[1], scopes)
            name = target[2]
            if isinstance(base, dict):
                old = base[name]
                if isinstance(old, V) and isinstance(value, V):
                    value = cast(value, old.k, len(old.c))
                newbase = dict(base)
                newbase[name] = value
                self.store(target[1], newbase, scopes)
            elif isinstance(base, V) and is_swizzle(name):
                comps = list(base.c)
                v = cast(value, base.k)
                vals = v.c * len(name) if len(v.c) == 1 else v.c
                if len(vals) < len(name):
                    raise EvalError(f'swizzle store {name} from {value}')
                for n, ch in enumerate(name):
                    comps[SWZ[ch]] = vals[n]
                self.store(target[1], V(base.k, comps), scopes)
            else:
                raise EvalError(f'store to member {name} of {base!r}')
        elif kind == 'index':
            base = self.eval(target[1], scopes)
            idx = int(self.scalar(self.eval(target[2], scopes)))
            if isinstance(base, list):
                nb = list(base)
                old = nb[idx]
                if isinstance(old, V) and isinstance(value, V):
                    value = cast(value, old.k, len(old.c))
                nb[idx] = value
                self.store(target[1], nb, scopes)
            elif isinstance(base, V):
                comps = list(base.c)
                comps[idx] = cast(value, base.k).c[0]
                self.store(target[1], V(base.k, comps), scopes)
            else:
                raise EvalError(f'indexed store into {base!r}')
        else:
            raise EvalError(f'not an lvalue: {target}')

    # ---- expressions
    def eval(self, e, scopes):
        kind = e[0]
        if kind == 'num':
            return parse_num(e[1])
        if kind == 'bool':
            return V('b', [e[1]])
        if kind == 'id':
            name = e[1]
            s = self.lookup(scopes, name)
            if s is not None:
                return s[name]
            if name in self.globals:
                return self.globals[name]
            raise EvalError(f'unknown identifier {name}')
        if kind == 'assign':
            op, lhs, rhs = e[1], e[2], e[3]
            val = self.eval(rhs, scopes)
            if op != '=':
                cur = self.eval(lhs, scopes)
                val = self.binop(op[:-1], cur, val)
            self.store(lhs, val, scopes)
            return self.eval(lhs, scopes)
        if kind == 'ternary':
            c = self.eval(e[1], scopes)
            if isinstance(c, V) and len(c.c) > 1:
                a = self.eval(e[2], scopes)
                b = self.eval(e[3], scopes)
                return self.select(c, a, b)
            return self.eval(e[2], scopes) if self.truth(c) else self.eval(e[3], scopes)
        if kind == 'bin':
            op = e[1]
            if op == '&&' or op == '||':
                a = self.eval(e[2], scopes)
                if isinstance(a, V) and len(a.c) == 1:
                    ta = bool(a.c[0])
                    if op == '&&' and not ta:
                        return V('b', [False])
                    if op == '||' and ta:
                        return V('b', [True])
                    b = self.eval(e[3], scopes)
                    return V('b', [bool(self.scalar(b))])
                raise EvalError('vector logical op')
            return self.binop(op, self.eval(e[2], scopes), self.eval(e[3], scopes))
        if kind == 'un':
            v = self.eval(e[2], scopes)
            op = e[1]
            if op == '-':
                if v.k == 'f':
                    return V('f', [F32(-x) for x in v.c])
                k = 'i' if v.k == 'b' else v.k
                return V(k, [conv(-int(x), k) for x in v.c])
            if op == '+':
                return v
            if op == '!':
                return V('b', [not bool(x) for x in v.c])
            if op == '~':
                k = 'i' if v.k == 'b' else v.k
                return V(k, [conv(~int(x), k) for x in v.c])
        if kind in ('preinc', 'postinc'):
            target = e[2]
            cur = self.eval(target, scopes)
            new = self.binop('+' if e[1] == '++' else '-', cur, V('i', [1]))
            self.store(target, new, scopes)
            return new if kind == 'preinc' else cur
        if kind == 'cast':
            typ = e[1]
            v = self.eval(e[2], scopes)
            ti = type_info(typ)
            if ti:
                return cast(v, ti[0], ti[1])
            if typ in self.p.structs:
                return zero_of(typ, self.p.structs)  # (Struct)0
            raise EvalError(f'cast to {typ}')
        if kind == 'member':
            base = self.eval(e[1], scopes)
            name = e[2]
            if isinstance(base, dict):
                return base[name]
            if isinstance(base, V):
                if not is_swizzle(name):
                    raise EvalError(f'member {name} of vector')
                return V(base.k, [base.c[SWZ[ch]] for ch in name])
            if isinstance(base, Res):
                if base.kind == 'ubo' and name == 'v':
                    return Res('ubov', base.index)
                if base.kind == 'push':
                    return Res('ptr', (name, 0))
                return ('method', base, name)
            raise EvalError(f'member {name} of {base!r}')
        if kind == 'index':
            base = self.eval(e[1], scopes)
            idx = self.eval(e[2], scopes)
            i = int(self.scalar(idx))
            if isinstance(base, list):
                if not 0 <= i < len(base):
                    raise EvalError(f'array index {i} out of range')
                return base[i]
            if isinstance(base, V):
                if not 0 <= i < len(base.c):
                    raise EvalError(f'component index {i}')
                return V(base.k, [base.c[i]])
            if isinstance(base, Res):
                if base.kind == 'ubov':
                    return self.mem.load(UBO_BLOCK[base.index], 16 * i, 'float4')
                if base.kind == 'heap':
                    return Res(base.index, i)
            raise EvalError(f'index into {base!r}')
        if kind == 'call':
            return self.call(e, scopes)
        if kind == 'comma':
            self.eval(e[1], scopes)
            return self.eval(e[2], scopes)
        raise EvalError(f'expression {kind}')

    def select(self, c, a, b):
        k = promote(a.k, b.k)
        n = max(len(c.c), len(a.c), len(b.c))
        cc = c.c * n if len(c.c) == 1 else c.c
        ac = a.c * n if len(a.c) == 1 else a.c
        bc = b.c * n if len(b.c) == 1 else b.c
        return V(k, [conv(ac[i], k) if cc[i] else conv(bc[i], k) for i in range(n)])

    def binop(self, op, a, b):
        if isinstance(a, Res) and a.kind == 'ptr' and op in '+-':
            block, off = a.index
            d = int(self.scalar(b))
            return Res('ptr', (block, off + d if op == '+' else off - d))
        if not isinstance(a, V) or not isinstance(b, V):
            raise EvalError(f'binop {op} on {a!r}, {b!r}')
        ac, bc = broadcast(a, b)
        if op in ('==', '!=', '<', '<=', '>', '>='):
            k = promote(a.k, b.k)
            if k == 'b':
                k = 'i'
            xs = [conv(x, k) for x in ac]
            ys = [conv(y, k) for y in bc]
            f = {'==': lambda x, y: x == y, '!=': lambda x, y: x != y, '<': lambda x, y: x < y,
                 '<=': lambda x, y: x <= y, '>': lambda x, y: x > y, '>=': lambda x, y: x >= y}[op]
            out = []
            for x, y in zip(xs, ys):
                if k == 'f' and (math.isnan(x) or math.isnan(y)):
                    out.append(op == '!=')
                else:
                    out.append(bool(f(x, y)))
            return V('b', out)
        if op in ('&', '|', '^', '<<', '>>'):
            k = promote(a.k, b.k)
            if k == 'f':
                raise EvalError(f'bitwise {op} on float')
            if k == 'b':
                if op in ('&', '|', '^'):
                    f = {'&': lambda x, y: x and y, '|': lambda x, y: x or y, '^': lambda x, y: x != y}[op]
                    return V('b', [bool(f(bool(x), bool(y))) for x, y in zip(ac, bc)])
                k = 'i'
            xs = [int(conv(x, k)) for x in ac]
            ys = [int(conv(y, k)) for y in bc]
            if k == 'u':
                xs = [x & 0xFFFFFFFF for x in xs]
            f = {'&': lambda x, y: x & y, '|': lambda x, y: x | y, '^': lambda x, y: x ^ y,
                 '<<': lambda x, y: x << (y & 31), '>>': lambda x, y: x >> (y & 31)}[op]
            return V(k, [conv(f(x, y), k) for x, y in zip(xs, ys)])
        k = promote(a.k, b.k)
        if k == 'b':
            k = 'i'
        xs = [conv(x, k) for x in ac]
        ys = [conv(y, k) for y in bc]
        if k == 'f':
            if op == '+':
                return V('f', [F32(x + y) for x, y in zip(xs, ys)])
            if op == '-':
                return V('f', [F32(x - y) for x, y in zip(xs, ys)])
            if op == '*':
                return V('f', [F32(x * y) for x, y in zip(xs, ys)])
            if op == '/':
                return V('f', [fdiv(x, y) for x, y in zip(xs, ys)])
            if op == '%':
                return V('f', [F32(math.fmod(x, y)) if y != 0 else F32('nan') for x, y in zip(xs, ys)])
        else:
            def idiv(x, y):
                if y == 0:
                    return -1 if k == 'u' else -1
                q = abs(x) // abs(y)
                return q if (x >= 0) == (y >= 0) else -q
            f = {'+': lambda x, y: x + y, '-': lambda x, y: x - y, '*': lambda x, y: x * y,
                 '/': idiv, '%': lambda x, y: (x - idiv(x, y) * y) if y else 0}[op]
            return V(k, [conv(f(int(x), int(y)), k) for x, y in zip(xs, ys)])
        raise EvalError(f'binop {op}')

    # ---- calls
    def call(self, e, scopes):
        fn = e[1]
        args = e[2]
        if fn[0] == 'member':
            base = self.eval(fn[1], scopes)
            if isinstance(base, Res):
                return self.method(base, fn[2], args, scopes)
            raise EvalError(f'method {fn[2]} on {base!r}')
        if fn[0] != 'id':
            raise EvalError(f'call of {fn}')
        name = fn[1]
        if name.startswith('vk::RawBufferLoad<'):
            typ = name[len('vk::RawBufferLoad<'):-1]
            ptr = self.eval(args[0], scopes)
            if not isinstance(ptr, Res) or ptr.kind != 'ptr':
                raise EvalError(f'RawBufferLoad of {ptr!r}')
            block, off = ptr.index
            return self.mem.load(PTR_BLOCK[block], off, typ)
        mi = mat_info(name)
        if mi:
            vals = [self.eval(a, scopes) for a in args]
            comps = []
            for v in vals:
                if isinstance(v, Mat):
                    for row in v:
                        comps.extend(conv(x, 'f') for x in row.c)
                else:
                    comps.extend(conv(x, 'f') for x in v.c)
            rows, cols = mi
            if len(comps) == 1:
                comps = comps * (rows * cols)
            if len(comps) != rows * cols:
                raise EvalError(f'{name} with {len(comps)} components')
            return Mat(V('f', comps[r * cols:(r + 1) * cols]) for r in range(rows))
        ti = type_info(name)
        if ti:
            vals = [self.eval(a, scopes) for a in args]
            comps = []
            for v in vals:
                if not isinstance(v, V):
                    raise EvalError(f'constructor {name} of {v!r}')
                comps.extend(conv(x, ti[0]) for x in v.c)
            if len(comps) == 1 and ti[1] > 1:
                comps = comps * ti[1]
            if len(comps) != ti[1]:
                if len(vals) == 1 and len(comps) > ti[1]:
                    comps = comps[:ti[1]]
                else:
                    raise EvalError(f'constructor {name} with {len(comps)} components')
            return V(ti[0], comps)
        user = self.p.functions.get(name)
        if user and len(args) in user:
            return self.call_user(user[len(args)], args, scopes)
        vals = [self.eval(a, scopes) for a in args]
        return self.intrinsic(name, vals, args, scopes)

    def call_user(self, fn, args, scopes):
        rtype, params, body = fn
        local = {}
        writeback = []
        for (mode, typ, name, sem), a in zip(params, args):
            if mode == 'out':
                local[name] = zero_of(typ, self.p.structs)
                writeback.append((name, a))
                continue
            val = self.eval(a, scopes)
            ti = type_info(typ)
            if ti and isinstance(val, V):
                val = cast(val, ti[0], ti[1])
            elif isinstance(val, dict):
                val = deepcopy(val)
            local[name] = val
            if mode == 'inout':
                writeback.append((name, a))
        result = None
        try:
            self.exec(body, [{}, local])  # globals are in self.globals
        except Return as r:
            result = r.value
        for name, a in writeback:
            self.store(a, local[name], scopes)
        ti = type_info(rtype)
        if ti and isinstance(result, V):
            result = cast(result, ti[0], ti[1])
        return result

    def method(self, res, name, args, scopes):
        vals = [self.eval(a, scopes) for a in args]
        if name == 'GetDimensions':
            w, h = self.tex.size(res.index)
            if len(args) >= 2:
                self.store(args[-2], V('u', [w]), scopes)
                self.store(args[-1], V('u', [h]), scopes)
            return None
        if name in ('Sample', 'SampleLevel', 'SampleBias', 'SampleGrad'):
            coord = [fval(x) for x in vals[1].c]
            if res.kind == 'tex2d':
                return V('f', self.tex.sample2d(res.index, coord[0], coord[1]))
            if res.kind == 'texcube':
                return V('f', self.tex.sample_cube(res.index, *coord[:3]))
            if res.kind == 'tex3d':
                return V('f', self.tex.sample3d(res.index, *coord[:3]))
        if name == 'GatherRed' and res.kind == 'tex2d':
            coord = [fval(x) for x in vals[1].c]
            return V('f', self.tex.gather_red(res.index, coord[0], coord[1]))
        if name == 'SampleCmpLevelZero':
            raise EvalError('SampleCmp')
        raise EvalError(f'method {name} on {res}')

    def intrinsic(self, name, vals, args, scopes):
        def fmap(f, v):
            v = cast(v, 'f') if v.k != 'f' else v
            return V('f', [F32(f(float(x))) for x in v.c])

        def fmap2(f, a, b):
            a = cast(a, 'f') if a.k != 'f' else a
            b = cast(b, 'f') if b.k != 'f' else b
            ac, bc = broadcast(a, b)
            return V('f', [F32(f(float(x), float(y))) for x, y in zip(ac, bc)])

        if name == 'abs':
            v = vals[0]
            if v.k == 'f':
                return V('f', [F32(abs(x)) for x in v.c])
            return V(v.k, [conv(abs(int(x)), v.k) for x in v.c])
        if name == 'saturate':
            return fmap(lambda x: 0.0 if math.isnan(x) else min(max(x, 0.0), 1.0), vals[0])
        if name in ('min', 'max'):
            a, b = vals
            k = promote(a.k, b.k)
            ac, bc = broadcast(a, b)
            out = []
            for x, y in zip(ac, bc):
                x, y = conv(x, k), conv(y, k)
                if k == 'f':
                    if math.isnan(x):
                        out.append(y)
                        continue
                    if math.isnan(y):
                        out.append(x)
                        continue
                out.append(min(x, y) if name == 'min' else max(x, y))
            return V(k, out)
        if name == 'clamp':
            v, lo, hi = vals
            return self.intrinsic('min', [self.intrinsic('max', [v, lo], None, None), hi], None, None)
        if name == 'sqrt':
            return fmap(lambda x: math.sqrt(x) if x >= 0 else (x if math.isnan(x) else float('nan')), vals[0])
        if name == 'rsqrt':
            return fmap(rsqrt, vals[0])
        if name == 'rcp':
            return fmap(lambda x: (math.copysign(float('inf'), x) if x == 0 else 1.0 / x), vals[0])
        if name == 'exp2':
            return fmap(exp2, vals[0])
        if name == 'exp':
            return fmap(lambda x: exp2(x * 1.4426950408889634), vals[0])
        if name == 'log2':
            return fmap(log2, vals[0])
        if name == 'log':
            return fmap(lambda x: log2(x) * 0.6931471805599453, vals[0])
        if name == 'pow':
            return fmap2(lambda x, y: exp2(y * log2(x)), vals[0], vals[1])
        if name == 'frac':
            return fmap(lambda x: x - math.floor(x) if math.isfinite(x) else float('nan'), vals[0])
        if name == 'floor':
            return fmap(lambda x: math.floor(x) if math.isfinite(x) else x, vals[0])
        if name == 'ceil':
            return fmap(lambda x: math.ceil(x) if math.isfinite(x) else x, vals[0])
        if name == 'trunc':
            return fmap(lambda x: math.trunc(x) if math.isfinite(x) else x, vals[0])
        if name == 'round':
            return fmap(lambda x: float(np.rint(x)) if math.isfinite(x) else x, vals[0])
        if name == 'sign':
            return fmap(lambda x: 0.0 if x == 0 or math.isnan(x) else math.copysign(1.0, x), vals[0])
        if name == 'sin':
            return fmap(lambda x: math.sin(x) if math.isfinite(x) else float('nan'), vals[0])
        if name == 'cos':
            return fmap(lambda x: math.cos(x) if math.isfinite(x) else float('nan'), vals[0])
        if name == 'dot':
            a, b = cast(vals[0], 'f'), cast(vals[1], 'f')
            ac, bc = broadcast(a, b)
            s = F32(0.0)
            for i, (x, y) in enumerate(zip(ac, bc)):
                s = F32(x * y) if i == 0 else F32(s + F32(x * y))
            return V('f', [s])
        if name == 'lerp':
            a, b, t = [cast(v, 'f') for v in vals]
            n = max(len(a.c), len(b.c), len(t.c))
            ex = lambda v: v.c * n if len(v.c) == 1 else v.c
            return V('f', [F32(x + F32(F32(y - x) * w)) for x, y, w in zip(ex(a), ex(b), ex(t))])
        if name in ('mad', 'fma'):
            return self.binop('+', self.binop('*', vals[0], vals[1]), vals[2])
        if name == 'select':
            return self.select(vals[0], vals[1], vals[2])
        if name == 'mul':
            a, b = vals
            if isinstance(a, Mat) and isinstance(b, Mat):
                n = len(b)
                out = Mat()
                for row in a:
                    comps = []
                    for j in range(len(b[0].c)):
                        acc = F32(0.0)
                        for k in range(n):
                            acc = F32(acc + F32(row.c[k] * b[k].c[j]))
                        comps.append(acc)
                    out.append(V('f', comps))
                return out
            if isinstance(a, Mat):
                return V('f', [self.intrinsic('dot', [row, b], None, None).c[0] for row in a])
            if isinstance(b, Mat):
                comps = []
                for j in range(len(b[0].c)):
                    acc = F32(0.0)
                    for k in range(len(b)):
                        acc = F32(acc + F32(a.c[k] * b[k].c[j]))
                    comps.append(acc)
                return V('f', comps)
            return self.binop('*', a, b)
        if name == 'isnan':
            return V('b', [math.isnan(float(x)) for x in cast(vals[0], 'f').c])
        if name == 'isinf':
            return V('b', [math.isinf(float(x)) for x in cast(vals[0], 'f').c])
        if name == 'isfinite':
            return V('b', [math.isfinite(float(x)) for x in cast(vals[0], 'f').c])
        if name == 'all':
            return V('b', [all(bool(x) for x in vals[0].c)])
        if name == 'any':
            return V('b', [any(bool(x) for x in vals[0].c)])
        if name == 'and':
            return self.binop('&', cast(vals[0], 'b'), cast(vals[1], 'b'))
        if name == 'or':
            return self.binop('|', cast(vals[0], 'b'), cast(vals[1], 'b'))
        if name == 'asfloat':
            v = vals[0]
            if v.k == 'f':
                return v
            return V('f', [F32(struct.unpack('<f', struct.pack('<I', int(x) & 0xFFFFFFFF))[0]) for x in v.c])
        if name in ('asuint', 'asint'):
            v = vals[0]
            k = 'u' if name == 'asuint' else 'i'
            if v.k == 'f':
                return V(k, [conv(struct.unpack('<I', struct.pack('<f', x))[0], k) for x in v.c])
            return V(k, [conv(int(x), k) for x in v.c])
        if name == 'normalize':
            v = cast(vals[0], 'f')
            d = self.intrinsic('dot', [v, v], None, None)
            r = rsqrt(float(d.c[0]))
            return V('f', [F32(x * F32(r)) for x in v.c])
        if name == 'length':
            v = cast(vals[0], 'f')
            d = self.intrinsic('dot', [v, v], None, None)
            return V('f', [F32(math.sqrt(float(d.c[0])))])
        if name == 'cross':
            a, b = cast(vals[0], 'f').c, cast(vals[1], 'f').c
            return V('f', [F32(a[1] * b[2] - a[2] * b[1]), F32(a[2] * b[0] - a[0] * b[2]), F32(a[0] * b[1] - a[1] * b[0])])
        if name in ('ddx', 'ddy', 'ddx_fine', 'ddy_fine', 'ddx_coarse', 'ddy_coarse', 'fwidth'):
            v = cast(vals[0], 'f')
            return V('f', [F32(0.0)] * len(v.c))
        if name in ('QuadReadAcrossX', 'QuadReadAcrossY', 'QuadReadAcrossDiagonal', 'WaveReadLaneFirst'):
            return vals[0]
        if name == 'clip':
            if any(float(x) < 0 for x in cast(vals[0], 'f').c):
                raise Discard()
            return None
        if name == 'step':
            return fmap2(lambda y, x: 1.0 if x >= y else 0.0, vals[0], vals[1])
        if name == 'smoothstep':
            raise EvalError('smoothstep')
        raise EvalError(f'unknown function {name}')


class LoopBreak(Exception):
    pass


class LoopContinue(Exception):
    pass


def deepcopy(v):
    if isinstance(v, Mat):
        return Mat(v)
    if isinstance(v, dict):
        return {k: deepcopy(x) for k, x in v.items()}
    if isinstance(v, list):
        return [deepcopy(x) for x in v]
    return v


def parse_num(s):
    t = s.rstrip('uUlLfFhH')
    suffix = s[len(t):].lower()
    if t.lower().startswith('0x'):
        v = int(t, 16)
        return V('u', [v & 0xFFFFFFFF]) if ('u' in suffix or v > 0x7FFFFFFF) else V('i', [wrap_i(v)])
    if any(ch in t for ch in '.eE') or 'f' in suffix or 'h' in suffix:
        return V('f', [F32(float(t))])
    v = int(t)
    return V('u', [v & 0xFFFFFFFF]) if 'u' in suffix else V('i', [wrap_i(v)])


def fdiv(x, y):
    x, y = float(x), float(y)
    if y == 0:
        if x == 0 or math.isnan(x):
            return F32('nan')
        return F32(math.copysign(float('inf'), x) * math.copysign(1.0, y))
    return F32(x / y)


def rsqrt(x):
    if math.isnan(x) or x < 0:
        return float('nan')
    if x == 0:
        return float('inf')
    if math.isinf(x):
        return 0.0
    return 1.0 / math.sqrt(x)


def exp2(x):
    if math.isnan(x):
        return x
    if x > 128:
        return float('inf')
    if x < -150:
        return 0.0
    return 2.0 ** x


def log2(x):
    if math.isnan(x) or x < 0:
        return float('nan')
    if x == 0:
        return float('-inf')
    if math.isinf(x):
        return x
    return math.log2(x)


UBO_BLOCK = {'vertex': 'vertex', 'pixel': 'pixel', 'shared': 'shared'}
PTR_BLOCK = {'VertexShaderConstants': 'vertex', 'PixelShaderConstants': 'pixel', 'SharedConstants': 'shared'}


def make_globals(spec, unused=(0, 0, 0)):
    g = {
        'g_SpecConstants': V('u', [spec]),
        'g_SpecUnusedOutputs0': V('u', [unused[0]]),
        'g_SpecUnusedOutputs1': V('u', [unused[1]]),
        'g_SpecUnusedOutputs2': V('u', [unused[2]]),
        'g_PushConstants': Res('push', None),
        'g_UboVertex': Res('ubo', 'vertex'),
        'g_UboPixel': Res('ubo', 'pixel'),
        'g_UboShared': Res('ubo', 'shared'),
        'g_Texture2DDescriptorHeap': Res('heap', 'tex2d'),
        'g_Texture3DDescriptorHeap': Res('heap', 'tex3d'),
        'g_TextureCubeDescriptorHeap': Res('heap', 'texcube'),
        'g_SamplerDescriptorHeap': Res('heap', 'sampler'),
    }
    return g
