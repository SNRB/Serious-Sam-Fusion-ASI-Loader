"""Minimal COFF (x64 .obj) reader and linker for the patch packages.

The features (mpm.c, cs.c, ...) are compiled by clang (--target=x86_64-pc-windows-msvc) into one COFF object each;
build_pkg.py places that object's sections into a memory image ("blob") at a chosen address and resolves its
relocations against the executable's addresses (the symbols of the configuration). This module only reads the
object; the placement itself is done by build_pkg.link_blob (or by link() below for the simple case).

    o = Obj(open('x.obj', 'rb').read())
    o.sections   list of dict(idx, name, ch, align, size, data (bytearray, zero filled for .bss), relocs, comdat)
                 relocs: list of (offset in section, symbol table index, IMAGE_REL_AMD64_* type)
    o.symbols    {symbol table index: dict(name, value, sec, type, cls, naux)}  (aux records skipped)
                 sec: section number (1-based), 0 undefined, -1 absolute, -2 debug
"""
import struct

IMAGE_FILE_MACHINE_AMD64 = 0x8664

IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_LNK_INFO = 0x00000200
IMAGE_SCN_LNK_REMOVE = 0x00000800
IMAGE_SCN_LNK_COMDAT = 0x00001000
IMAGE_SCN_ALIGN_MASK = 0x00f00000
IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000
IMAGE_SCN_MEM_DISCARDABLE = 0x02000000
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000

IMAGE_REL_AMD64_ABSOLUTE = 0
IMAGE_REL_AMD64_ADDR64 = 1
IMAGE_REL_AMD64_ADDR32 = 2
IMAGE_REL_AMD64_ADDR32NB = 3
IMAGE_REL_AMD64_REL32 = 4          # .. REL32_5 = 9
IMAGE_REL_AMD64_SECTION = 10
IMAGE_REL_AMD64_SECREL = 11

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3
IMAGE_SYM_CLASS_LABEL = 6
IMAGE_SYM_CLASS_FILE = 103


def align(x, a):
    return (x + a - 1) & ~(a - 1)


class Obj:
    def __init__(self, data):
        self.raw = data
        machine, nsec, ts, symptr, nsym, optsz, ch = struct.unpack_from('<HHIIIHH', data, 0)
        assert machine == IMAGE_FILE_MACHINE_AMD64, 'not an x64 COFF object (machine %#x)' % machine
        self.strtab_off = symptr + 18 * nsym
        # sections
        self.sections = []
        for i in range(nsec):
            o = 20 + optsz + 40 * i
            name = data[o:o + 8].rstrip(b'\0').decode('latin-1')
            vsize, va, rsize, rptr, relptr, lnptr, nrel, nln, sch = struct.unpack_from('<IIIIIIHHI', data, o + 8)
            if name.startswith('/'):
                name = self._str(int(name[1:]))
            n = (sch & IMAGE_SCN_ALIGN_MASK) >> 20
            a = 1 << (n - 1) if n else 1
            if rptr and not sch & IMAGE_SCN_CNT_UNINITIALIZED_DATA:
                sdata = bytearray(data[rptr:rptr + rsize])
            else:
                sdata = bytearray(rsize)
            relocs = []
            if sch & IMAGE_SCN_LNK_NRELOC_OVFL and nrel == 0xffff:
                nrel = struct.unpack_from('<I', data, relptr)[0]   # the first record holds the real count
                relptr += 10
                nrel -= 1
            for k in range(nrel):
                rva, si, typ = struct.unpack_from('<IIH', data, relptr + 10 * k)
                relocs.append((rva, si, typ))
            self.sections.append(dict(idx=i + 1, name=name, ch=sch, align=a, size=rsize, data=sdata,
                                      relocs=relocs, comdat=None))
        # symbols
        self.symbols = {}
        i = 0
        while i < nsym:
            o = symptr + 18 * i
            if data[o:o + 4] == b'\0\0\0\0':
                name = self._str(struct.unpack_from('<I', data, o + 4)[0])
            else:
                name = data[o:o + 8].rstrip(b'\0').decode('latin-1')
            value, sec, typ, cls, naux = struct.unpack_from('<IhHBB', data, o + 8)
            sym = dict(name=name, value=value, sec=sec, type=typ, cls=cls, naux=naux)
            # section definition aux record: COMDAT selection
            if cls == IMAGE_SYM_CLASS_STATIC and naux and 0 < sec <= nsec and value == 0:
                ao = o + 18
                sel = data[ao + 14]
                if self.sections[sec - 1]['ch'] & IMAGE_SCN_LNK_COMDAT:
                    self.sections[sec - 1]['comdat'] = sel
            self.symbols[i] = sym
            i += 1 + naux

    def _str(self, off):
        d = self.raw
        e = d.index(b'\0', self.strtab_off + off)
        return d[self.strtab_off + off:e].decode('latin-1')

    def section(self, idx):
        return self.sections[idx - 1]

    def find(self, name):
        """(section index, value) of a defined symbol"""
        for sym in self.symbols.values():
            if sym['name'] == name and sym['sec'] > 0:
                return sym['sec'], sym['value']
        raise KeyError(name)


def link(obj_bytes, va_code, va_data, image_base, symbol_map):
    """Places one object: code + read-only sections from va_code, writable ones from va_data, and resolves the
    relocations (ADDR32NB relative to image_base). Returns dict(code, data, pdata, symbols). build_pkg.link_blob is
    the same with fixup recording."""
    o = Obj(obj_bytes)
    code_secs, ro_secs, data_secs, pdata_secs = [], [], [], []
    for s in o.sections:
        n, ch = s['name'], s['ch']
        if ch & IMAGE_SCN_LNK_REMOVE or n.startswith('.debug') or n in ('.drectve', '.llvm_addrsig') or n.startswith('.llvm'):
            continue
        if n.startswith('.pdata'):
            pdata_secs.append(s)
        elif ch & IMAGE_SCN_CNT_CODE:
            code_secs.append(s)
        elif ch & IMAGE_SCN_MEM_WRITE:
            data_secs.append(s)
        else:
            ro_secs.append(s)
    addr = {}
    cur = va_code
    for s in code_secs + ro_secs:
        cur = align(cur, max(s['align'], 4))
        addr[s['idx']] = cur
        cur += s['size']
    code_end = cur
    cur = va_data
    for s in data_secs:
        cur = align(cur, max(s['align'], 8))
        addr[s['idx']] = cur
        cur += s['size']
    data_end = cur

    def symva(si):
        sym = o.symbols[si]
        if sym['sec'] > 0:
            return addr[sym['sec']] + sym['value']
        if sym['sec'] == -1:
            return sym['value']
        if sym['name'] in symbol_map:
            return symbol_map[sym['name']]
        raise KeyError('unresolved external %s' % sym['name'])

    for s in code_secs + ro_secs + data_secs + pdata_secs:
        base = addr.get(s['idx'], 0)
        buf = s['data']
        for rva, si, typ in s['relocs']:
            S = symva(si)
            P = base + rva
            if typ == IMAGE_REL_AMD64_ADDR64:
                A = struct.unpack_from('<q', buf, rva)[0]
                struct.pack_into('<Q', buf, rva, (S + A) & 0xffffffffffffffff)
            elif typ == IMAGE_REL_AMD64_ADDR32NB:
                A = struct.unpack_from('<i', buf, rva)[0]
                struct.pack_into('<I', buf, rva, (S + A - image_base) & 0xffffffff)
            elif IMAGE_REL_AMD64_REL32 <= typ <= IMAGE_REL_AMD64_REL32 + 5:
                A = struct.unpack_from('<i', buf, rva)[0]
                v = S + A - (P + 4 + typ - IMAGE_REL_AMD64_REL32)
                assert -(1 << 31) <= v < 1 << 31, 'rel32 out of range in %s' % s['name']
                struct.pack_into('<i', buf, rva, v)
            else:
                raise ValueError('unsupported reloc type %d in %s' % (typ, s['name']))
    code = bytearray(code_end - va_code)
    for s in code_secs + ro_secs:
        off = addr[s['idx']] - va_code
        code[off:off + s['size']] = s['data']
    data = bytearray(data_end - va_data)
    for s in data_secs:
        off = addr[s['idx']] - va_data
        data[off:off + s['size']] = s['data']
    pdata = []
    for s in pdata_secs:
        for i in range(0, len(s['data']), 12):
            pdata.append(struct.unpack_from('<III', s['data'], i))
    syms = {}
    for si, sym in o.symbols.items():
        if sym['sec'] > 0 and sym['sec'] in addr and sym['cls'] in (2, 3) and sym['name']:
            syms.setdefault(sym['name'], addr[sym['sec']] + sym['value'])
    return dict(code=code, data=data, pdata=pdata, symbols=syms)
