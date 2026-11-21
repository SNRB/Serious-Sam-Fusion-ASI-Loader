"""Cross-reference index for a fixed-base PE (numpy based)."""
import numpy as np, os, sys, struct
sys.path.insert(0, os.path.dirname(__file__))
from pe import PE, disasm
import os
BIN = os.environ.get('SAM2017_BIN', '.')
EXE = {'ed': os.path.join(BIN, 'Sam2017_SeriousEditor.exe'), 'gm': os.path.join(BIN, 'Sam2017_Unrestricted.exe')}

_cache = {}

class XR:
    def __init__(self, path):
        self.pe = PE(path)
        self.path = path
        pe = self.pe
        s = pe.sec('.text')
        self.tbase = pe.imagebase + s[1]
        raw = np.frombuffer(pe.data, dtype=np.uint8, count=s[4], offset=s[3])
        self.traw = raw
        d = raw.astype(np.uint32)
        disp = (d[:-3] | (d[1:-2] << 8) | (d[2:-1] << 16) | (d[3:] << 24)).view(np.int32).astype(np.int64)
        idx = np.arange(len(disp), dtype=np.int64)
        self.t0 = self.tbase + idx + 4 + disp  # disp is last field of instruction
        del d, idx
        self.disp = disp
        # data qword views (8-aligned) for .rdata and .data
        self.qsecs = []
        for name in ('.rdata', '.data'):
            ss = pe.sec(name)
            n = ss[4] // 8
            q = np.frombuffer(pe.data, dtype=np.uint64, count=n, offset=ss[3])
            self.qsecs.append((pe.imagebase + ss[1], q))

    def code_refs(self, target, extras=(0, 1, 4)):
        """Return list of VAs of the disp32 field that refer to target."""
        out = []
        for ex in extras:
            # t0 = base+i+4+disp ; with extra imm: target = t0 + ex
            hits = np.nonzero(self.t0 == (target - ex))[0]
            for h in hits:
                out.append((int(self.tbase + h), ex))
        return sorted(out)

    def data_refs(self, target):
        out = []
        for base, q in self.qsecs:
            hits = np.nonzero(q == np.uint64(target))[0]
            out += [int(base + 8 * h) for h in hits]
        return out

    def calls_to(self, target):
        """E8 call sites to target."""
        out = []
        for va, ex in self.code_refs(target, (0,)):
            off = va - self.tbase
            if off >= 1 and self.traw[off - 1] == 0xE8:
                out.append(va - 1)
        return out

    def jmps_to(self, target):
        out = []
        for va, ex in self.code_refs(target, (0,)):
            off = va - self.tbase
            if off >= 1 and self.traw[off - 1] == 0xE9:
                out.append(va - 1)
        return out

    def func_start(self, va):
        """Find function start from .pdata (RUNTIME_FUNCTION table)."""
        return self.func_bounds(va)[0]

    def func_bounds(self, va):
        pe = self.pe
        if not hasattr(self, '_pdata'):
            rva, size = pe.dirs[3]
            off = pe.rva2off(rva)
            arr = np.frombuffer(pe.data, dtype=np.uint32, count=size // 4, offset=off).reshape(-1, 3)
            self._pdata = arr
        arr = self._pdata
        rva = va - pe.imagebase
        i = np.searchsorted(arr[:, 0], rva, side='right') - 1
        while i >= 0:
            b, e, u = arr[i]
            if b <= rva < e:
                # follow chained unwind info to primary function start
                return self._primary(int(b), int(e), int(u))
            i -= 1
            break
        return None

    def _primary(self, b, e, u):
        pe = self.pe
        # unwind info: if flags & UNW_FLAG_CHAININFO (4), chained RUNTIME_FUNCTION follows
        start, end = b, e
        for _ in range(16):
            ui = pe.rva2off(u & ~1 if u & 1 == 0 else u)
            if u & 1:  # indirect
                break
            hdr = pe.data[ui]
            flags = hdr >> 3
            if not (flags & 4):
                break
            cnt = pe.data[ui + 2]
            co = ui + 4 + ((cnt + 1) & ~1) * 2
            b2, e2, u2 = struct.unpack_from('<III', pe.data, co)
            start = b2
            u = u2
        return (pe.imagebase + start, pe.imagebase + end, pe.imagebase + b)

    def dis(self, start, end):
        return disasm(self.path, start, end)

    def dis_func(self, va):
        fb = self.func_bounds(va)
        if fb is None:
            return None
        return self.dis(fb[2], fb[1])


def get(name):
    if name not in _cache:
        path = EXE[name]
        _cache[name] = XR(path)
    return _cache[name]


def _build_frag_map(self):
    pe = self.pe
    rva, size = pe.dirs[3]
    off = pe.rva2off(rva)
    arr = np.frombuffer(pe.data, dtype=np.uint32, count=size // 4, offset=off).reshape(-1, 3)
    self._pdata = arr
    prim = {}
    d = pe.data
    for b, e, u in arr.tolist():
        start = b
        uu = u
        for _ in range(32):
            if uu & 1:
                break
            ui = pe.rva2off(uu)
            if ui is None:
                break
            flags = d[ui] >> 3
            if not (flags & 4):
                break
            cnt = d[ui + 2]
            co = ui + 4 + ((cnt + 1) & ~1) * 2
            b2, e2, u2 = struct.unpack_from('<III', d, co)
            start = b2
            uu = u2
        prim.setdefault(start, []).append((b, e))
    self._frags = prim
    # map from fragment begin to primary
    self._frag2prim = {}
    for p, fl in prim.items():
        for b, e in fl:
            self._frag2prim[b] = p

def func_frags(self, va):
    """Return sorted list of (start_va, end_va) fragments of the function containing va."""
    if not hasattr(self, '_frags'):
        _build_frag_map(self)
    arr = self._pdata
    rva = va - self.pe.imagebase
    i = np.searchsorted(arr[:, 0], rva, side='right') - 1
    if i < 0:
        return None
    b, e, u = arr[i]
    if not (b <= rva < e):
        return None
    p = self._frag2prim[int(b)]
    base = self.pe.imagebase
    return sorted((base + s, base + t) for s, t in self._frags[p])

def dis_full(self, va):
    fr = self.func_frags(va)
    out = []
    for s, t in fr:
        out.append('--- fragment %x-%x' % (s, t))
        out.append(self.dis(s, t))
    return '\n'.join(out)

XR.func_frags = func_frags
XR.dis_full = dis_full
