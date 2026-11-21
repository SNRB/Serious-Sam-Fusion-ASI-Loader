"""Function copies ("clones") for the package builder (build_pkg.py).

A feature can carry a copy of an executable function in its blob - CellShaded copies the Standard shader's render
function and changes a few of its references, so that the copy draws with the CellShaded pixel programs while the
original stays untouched. The copy is relocated for its new address:

  - rel32 branches (call/jmp/jcc) and RIP-relative operands that point outside the function: the displacement is
    recomputed for the copy's address (a 'rel32_out' fixup, as the executable stays where it is when the blob moves);
    references inside the function are relative and stay as they are;
  - 'redirect' {executable address: symbol}: references to that address (branch target, RIP-relative operand or an
    image-base-relative displacement [reg + RVA], as MSVC addresses arrays off __ImageBase) point to the symbol of the
    feature instead (inside the blob: no fixup for rel32, an 'imgrel32' fixup for an image-relative displacement);
  - 'replace' [[va, old bytes, new bytes], ...]: byte changes in the copy (the old bytes are checked);
  - the function's unwind information (all RUNTIME_FUNCTION entries of the function, the chained ones included) is
    copied next to the blob's own, with the chain links pointing to the copies; RVAs are relative to the blob base
    (the plugin registers the blob's function table with RtlAddFunctionTable(table, n, blob base)).

Instructions are decoded with objdump (binutils). Configuration entry (cfg 'clones'):
    {"sym": "CS_StandardRender", "start": "0x140484e70", "size": "0x28c1",
     "redirect": {"0x1405176a0": "CS_...", ...}, "replace": [["0x1404872da", "41b916000000", "41b919000000"], ...]}
"""
import re, struct, subprocess, tempfile, os
from patch_exe3 import u8, u16, u32, s32, u64

UNW_FLAG_EHANDLER, UNW_FLAG_UHANDLER, UNW_FLAG_CHAININFO = 1, 2, 4


def H(x):
    """config value -> int (hex strings like '0x1404872da' or plain ints)"""
    return int(x, 16) if isinstance(x, str) else x


def disassemble(code, va):
    """[(va, length, bytes, text)] of the instructions of code placed at va"""
    fd, path = tempfile.mkstemp(suffix='.bin')
    try:
        os.write(fd, code)
        os.close(fd)
        out = subprocess.run(['objdump', '-D', '--insn-width=16', '-b', 'binary', '-m', 'i386:x86-64',
                              '--adjust-vma=%#x' % va, path], capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(path)
    res = []
    for line in out.splitlines():
        m = re.match(r'\s+([0-9a-f]+):\t([0-9a-f ]+)\t(.*)$', line)
        if m:
            b = bytes.fromhex(m.group(2).replace(' ', ''))
            res.append((int(m.group(1), 16), len(b), b, m.group(3).strip()))
    return res


def _find_field(insn, value):
    """offset of the (unique) 4-byte little-endian field holding value inside the instruction bytes"""
    pat = struct.pack('<i', value) if value < 0 else struct.pack('<I', value & 0xffffffff)
    hits = [i for i in range(1, len(insn) - 3) if insn[i:i + 4] == pat]
    assert len(hits) == 1, 'cannot locate field %#x in %s' % (value, insn.hex())
    return hits[0]


class Clone:
    def __init__(self, pe, exe_path, cfg):
        self.pe, self.exe_path, self.cfg = pe, exe_path, cfg
        self.sym = cfg['sym']
        self.start = H(cfg['start'])
        base = pe.image_base
        # the function's RUNTIME_FUNCTION entries: the primary one at start, then the contiguous chained ones
        first = pe.function_entry(self.start)
        assert first and first[0] + base == self.start, 'no function entry at %#x' % self.start
        rf = pe.runtime_functions()
        i = rf.index(first)
        ents = [first]
        while i + 1 < len(rf) and rf[i + 1][0] == ents[-1][1] and self._chained_to(rf[i + 1], first):
            i += 1
            ents.append(rf[i])
        self.entries = ents
        self.size = ents[-1][1] - ents[0][0]
        if 'size' in cfg:
            assert H(cfg['size']) == self.size, 'function size %#x, configured %#x' % (self.size, H(cfg['size']))
        self.code = bytes(pe.read(self.start, self.size))
        # unwind information to copy, one block per entry (in entry order)
        self.unwind = []
        for b, e, u in ents:
            ui = pe.read(base + u, 4)
            flags, ncodes = u8(ui, 0) >> 3, u8(ui, 2)
            assert not flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER), 'exception handlers are not supported'
            n = 4 + 2 * ((ncodes + 1) & ~1) + (12 if flags & UNW_FLAG_CHAININFO else 0)
            self.unwind.append((u, bytes(pe.read(base + u, n)), flags))
        self.unwind_size = sum((len(x[1]) + 3) & ~3 for x in self.unwind)
        self.pdata = ents

    def _chained_to(self, ent, root):
        base, seen = self.pe.image_base, 0
        while seen < 16:
            ui = self.pe.read(base + ent[2], 4)
            flags, ncodes = u8(ui, 0) >> 3, u8(ui, 2)
            if not flags & UNW_FLAG_CHAININFO:
                return ent == root
            o = 4 + 2 * ((ncodes + 1) & ~1)
            ent = struct.unpack('<III', self.pe.read(base + ent[2] + o, 12))
            seen += 1
        return False

    def build(self, va, symbols, uw_va, rva_base, fixups, is_internal):
        """the copy placed at va (unwind blocks at uw_va): (code, unwind bytes, pdata entries, report lines);
        appends (offset in the copy, kind) to fixups"""
        pe, start, size = self.pe, self.start, self.size
        code = bytearray(self.code)
        redirect = {H(k): symbols[v] for k, v in self.cfg.get('redirect', {}).items()}
        used = set()
        rep = ['clone %s: %#x..%#x (%#x bytes) at %#x' % (self.sym, start, start + size, size, va)]
        nrel = 0
        for a, n, b, t in disassemble(self.code, start):
            off = a - start
            end = a + n
            m = re.search(r'(?:call|jmp|j[a-z]+)\s+0x([0-9a-f]+)$', t)
            if m and not t.startswith(('jmp    *', 'call   *')):
                target = int(m.group(1), 16)
                if start <= target < start + size:
                    continue                                    # inside the copy: relative, unchanged
                assert n >= 5 and b[-4:] == struct.pack('<i', target - end), 'short branch out of the function at %#x' % a
                fo = off + n - 4
                new = redirect.get(target, target)
                if target in redirect:
                    used.add(target)
                    rep.append('  +%#x %s -> %#x (redirected)' % (off, t.split()[0], new))
                v = new - (va + off + n)
                assert -(1 << 31) <= v < 1 << 31
                struct.pack_into('<i', code, fo, v)
                if not is_internal(new):
                    fixups.append((fo, 'rel32_out'))
                nrel += 1
                continue
            m = re.search(r'([-]?0x[0-9a-f]+)?\(%rip\)', t)
            if m:
                disp = int(m.group(1), 16) if m.group(1) else 0
                target = end + disp
                if start <= target < start + size:
                    continue
                fo = off + _find_field(b, disp)
                tail = n - (fo - off) - 4                       # immediate bytes after the displacement
                new = redirect.get(target, target)
                if target in redirect:
                    used.add(target)
                    rep.append('  +%#x rip-relative %#x -> %#x (redirected)' % (off, target, new))
                v = new - (va + off + n)
                assert -(1 << 31) <= v < 1 << 31 and tail >= 0
                struct.pack_into('<i', code, fo, v)
                if not is_internal(new):
                    fixups.append((fo, 'rel32_out'))
                nrel += 1
                continue
            # image-base-relative displacement [reg + reg*s + RVA] of a redirected address
            for mm in re.finditer(r'(?<![\w$])0x([0-9a-f]+)\(%r', t):
                disp = int(mm.group(1), 16)
                target = pe.image_base + disp
                if target in redirect:
                    fo = off + _find_field(b, disp)
                    new = redirect[target]
                    v = new - pe.image_base
                    assert 0 <= v < 1 << 31
                    struct.pack_into('<I', code, fo, v)
                    if is_internal(new):
                        fixups.append((fo, 'imgrel32'))
                    used.add(target)
                    rep.append('  +%#x image-relative %#x -> %#x (redirected)' % (off, target, new))
        for va_r, old, new in self.cfg.get('replace', []):
            o = H(va_r) - start
            old, new = bytes.fromhex(old), bytes.fromhex(new)
            assert len(old) == len(new) and bytes(self.code[o:o + len(old)]) == old, 'replace at %#x' % H(va_r)
            code[o:o + len(new)] = new
            rep.append('  +%#x bytes %s -> %s' % (o, old.hex(), new.hex()))
        missing = set(redirect) - used
        assert not missing, 'redirected addresses not referenced: %s' % ', '.join('%#x' % x for x in sorted(missing))
        rep.append('  %d references relocated' % nrel)
        # unwind information and function entries
        base = pe.image_base
        uw_rva = {}
        cur = uw_va
        for u, blk, flags in self.unwind:
            uw_rva[u] = cur - rva_base
            cur += (len(blk) + 3) & ~3
        f0 = self.entries[0][0]                     # RVA of the function in the executable
        rel = va - rva_base                         # RVA of the copy in the blob

        def moved(rva):
            assert f0 <= rva <= f0 + size, 'chained entry outside the function'
            return rva - f0 + rel
        ub = bytearray()
        pd = []
        for (b, e, u), (u2, blk, flags) in zip(self.entries, self.unwind):
            blk = bytearray(blk)
            if flags & UNW_FLAG_CHAININFO:
                cb, ce, cu = struct.unpack_from('<III', blk, len(blk) - 12)
                assert cu in uw_rva, 'chained unwind info outside the function'
                struct.pack_into('<III', blk, len(blk) - 12, moved(cb), moved(ce), uw_rva[cu])
            ub += blk + b'\0' * (((len(blk) + 3) & ~3) - len(blk))
            pd.append((moved(b), moved(e), uw_rva[u]))
        rep.append('  %d unwind entries' % len(pd))
        return bytes(code), bytes(ub), pd, rep
