"""Builds the relocatable patch packages that the ASI plugins apply at run time to the ORIGINAL executables.

A package = one feature (MultiPathMarker, EditorScriptFunctions, CellShaded) for one executable:
  - a memory image ("blob") of the feature's code and data, linked at a nominal address just above the executable image,
    in the same way patch_exe5.py builds the extra sections of the build 5 executables (same objects source, same configs,
    same function copy for CellShaded), with:
      [clones][code + read-only data + unwind infos][clone unwind infos][function table]  -> page aligned, read/execute
      [data + bss]                                                                       -> read/write
  - fixups: every field that changes when the blob is placed at another address
      SELF64     64-bit pointer into the blob                                 += delta
      REL32_OUT  rel32 from the blob to the executable                        -= delta
      IMGREL32   image-base-relative displacement of a blob symbol (clone)    += delta
    (rel32 inside the blob, absolute pointers to the executable and the function table, whose RVAs are relative to
    the blob base, do not change)
  - patches to the executable, each with the value that must be there before: REL32 (call/jmp e8/e9 + rel32) and
    ABS64 (vtable entries)
  - checks: FNV-1a 64 hashes of executable ranges the blob depends on (copied function, copied vtables, patch contexts)
  - identity of the executable: TimeDateStamp, SizeOfImage, CheckSum; security cookie address and initial value
    (to detect being loaded after the C runtime has started)

Self-check: every blob is linked at two addresses; applying the fixups to the first must give the second exactly.

usage: build_pkg.py <out dir> [feature]

The original executables are read from exe/ next to this script, or from the paths in the environment variables
SAM2017_EDITOR_EXE and SAM2017_GAME_EXE. Needs clang, objdump (binutils) and Python 3.
"""
import sys, os, struct, json, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'tools'))
from coff_link import Obj, align, IMAGE_SCN_CNT_CODE, IMAGE_SCN_LNK_REMOVE, IMAGE_SCN_MEM_WRITE
from patch_exe3 import PEFile, u32, u64
from patch_exe5 import Clone, H

MAGIC = 0x41373153          # 'S17A'
VERSION = 1
FIX_SELF64, FIX_REL32_OUT, FIX_IMGREL32 = 0, 1, 2
FIX_KIND = {'self64': FIX_SELF64, 'rel32_out': FIX_REL32_OUT, 'imgrel32': FIX_IMGREL32}
PATCH_REL32, PATCH_ABS64, PATCH_BYTES = 1, 2, 3
FLAG_PRE_CRT = 1
HDR_SIZE = 192

EXES = {
    # the editor is the ORIGINAL, unmodified 7 June 2019 exe the user runs (identity: CheckSum 0x2d60797)
    'editor': dict(path=os.environ.get('SAM2017_EDITOR_EXE', os.path.join(HERE, 'exe', 'Sam2017_SeriousEditor.exe')),
                   cfg=os.path.join(HERE, 'cfg', 'editor5.json'), name='Sam2017_SeriousEditor.exe'),
    'game': dict(path=os.environ.get('SAM2017_GAME_EXE', os.path.join(HERE, 'exe', 'Sam2017_Unrestricted.exe')),
                 cfg=os.path.join(HERE, 'cfg', 'unrestricted5.json'), name='Sam2017_Unrestricted.exe'),
}

# Pure byte-patch features: a fixed list of (VA, original bytes, new bytes) per exe, with no code of their own.
# AnimButtonTweak reproduces the user's own one-byte editor change (file offset 0x186B78B, VA 0x14186C38B: 0x66 -> 0x62).
BYTE_FEATURES = {
    'AnimButtonTweak': dict(exes=('editor',),
                            patches={'editor': [(0x14186c38b, b'\x66', b'\x62')]}),
    # The game (not the editor) runs its own executable-integrity check shortly after the intro. On failure it calls the
    # handler at 0x1407e5900, which *reports* the failure (UTF-16 msg "Signature verification of executable failed." at
    # 0x141796d40, its only xref) via 0x140daf590 at 0x1407e5926 - and for this error severity that report path IS the
    # engine's fatal response: it shows the dialog, runs the crash reporter (dumping the stack + module list - that dump
    # is the crash log) and exits. The crash in the log (RIP 0x141001889) is inside that reporting machinery, reached
    # straight from this handler. So the handler must be stopped BEFORE it reports, not after (an earlier patch at
    # 0x1407e5939 sat after the report call and so never helped).
    # Fix: make the handler return "continue" (eax=1, the value its own non-fatal paths return) at its first instruction,
    # before it reports anything. Entry  40 57 48 83 ec 20  (rex push rdi; sub rsp,0x20)  ->  b8 01 00 00 00 c3
    # (mov eax,1; ret): rsp stays balanced (nothing pushed), rdi untouched. The verifier still runs and still fails;
    # only its fatal response is neutralised. Nothing about the check or any DRM is changed. The handler hardcodes the
    # signature message, so it is signature-specific - other engine errors are unaffected.
    'SkipExeSignatureCheck': dict(exes=('game',),
                                  patches={'game': [(0x1407e5900, b'\x40\x57\x48\x83\xec\x20',
                                                     b'\xb8\x01\x00\x00\x00\xc3')]}),
}
# feature: (source, prefix of its symbols, executables, slot of its nominal address above the image; text files the
# source #includes as C string literals: <name>.inc is generated next to the object)
SRC = os.path.join(HERE, 'src')
FEATURES = {
    'MultiPathMarker': dict(src=os.path.join(SRC, 'mpm.c'), prefixes=('MPM_', 'g_PropsVtbl', 'g_EntityVtbl'),
                            exes=('editor', 'game'), slot=0),
    'EditorScriptFunctions': dict(src=os.path.join(SRC, 'sra.c'), prefixes=('SRA_',), exes=('editor',), slot=1),
    'CellShaded': dict(src=os.path.join(SRC, 'cs.c'), prefixes=('CS_', 'g_CS', 'g_cs'), exes=('editor', 'game'), slot=2,
                       embed=('CellShadedShader.psh',)),
    'MultiSpawnerOrder': dict(src=os.path.join(SRC, 'ms.c'), prefixes=('MS_', 'g_ms', 's_ms'), exes=('editor', 'game'), slot=3),
}
SLOT_SIZE = 0x100000
CFLAGS = ['--target=x86_64-pc-windows-msvc', '-O2', '-fno-builtin', '-I' + SRC]


def fnv64(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h


def owner(name):
    for f, d in FEATURES.items():
        if name.startswith(d['prefixes']):
            return f
    raise KeyError('no feature owns %s' % name)


def split_cfg(cfg):
    """the build 5 configuration of one executable -> {feature: config}; every entry must belong to exactly one"""
    out = {f: dict(symbols=cfg['symbols'], vtable_patches=[], call_patches=[], sinit_hooks=[], vtable_fills=[],
                   data_patches=[], clones=[]) for f in FEATURES}
    for k in ('vtable_patches', 'call_patches', 'sinit_hooks'):
        for e in cfg.get(k, []):
            out[owner(e['to'])][k].append(e)
    for k in ('vtable_fills', 'data_patches', 'clones'):
        for e in cfg.get(k, []):
            out[owner(e['sym'])][k].append(e)
    if 'base_props_vtbl' in cfg:
        f = owner('g_PropsVtbl')
        out[f]['vtable_fills'] += [dict(sym='g_PropsVtbl', base=cfg['base_props_vtbl'], n=cfg['props_vtbl_entries']),
                                   dict(sym='g_EntityVtbl', base=cfg['base_entity_vtbl'], n=cfg['entity_vtbl_entries'])]
    return out


def embed_text(src, dst):
    """a text file as the body of a C string literal, one quoted line per line:
           static const char x[] =
           #include "file.inc"
           ;                                            (clang 18 has no #embed)"""
    text = open(src, 'rb').read().decode('ascii')
    assert '\\' not in text and '\r' not in text and '\0' not in text and '??' not in text
    lines = text.split('\n')
    if text.endswith('\n'):
        lines.pop()
    out = ['"%s\\n"' % line.replace('"', '\\"') for line in lines]
    if not text.endswith('\n'):
        out[-1] = out[-1][:-3] + '"'
    open(dst, 'w').write('\n'.join(out) + '\n')


def compile_obj(feature, objdir):
    fd = FEATURES[feature]
    src = fd['src']
    obj = os.path.join(objdir, feature + '.obj')
    for name in fd.get('embed', ()):
        embed_text(os.path.join(os.path.dirname(src), name), os.path.join(objdir, name + '.inc'))
    subprocess.check_call(['clang'] + CFLAGS + ['-I' + os.path.abspath(objdir), '-c', src, '-o', obj])
    return open(obj, 'rb').read()


def link_blob(obj_bytes, va_code, va_data, rva_base, symbol_map, is_internal):
    """coff_link.link with RVAs (ADDR32NB) relative to rva_base and a list of fixups (va of the field, kind)"""
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
    fixups = []

    def symva(si):
        sym = o.symbols[si]
        if sym['sec'] > 0:
            return addr[sym['sec']] + sym['value']
        if sym['sec'] == -1:
            return sym['value']
        if sym['name'] in symbol_map:
            return symbol_map[sym['name']]
        raise KeyError('unresolved external %s' % sym['name'])

    def apply(sec, base_va, record):
        buf = sec['data']
        for rva, si, typ in sec['relocs']:
            S = symva(si)
            P = base_va + rva
            if typ == 1:                                   # ADDR64
                A = struct.unpack_from('<q', buf, rva)[0]
                struct.pack_into('<Q', buf, rva, (S + A) & 0xffffffffffffffff)
                if record and is_internal(S):
                    fixups.append((P, 'self64'))
            elif typ == 3:                                 # ADDR32NB
                A = struct.unpack_from('<i', buf, rva)[0]
                assert is_internal(S), 'ADDR32NB to %#x outside the blob' % S
                v = S + A - rva_base
                assert 0 <= v < 1 << 31
                struct.pack_into('<I', buf, rva, v)
            elif 4 <= typ <= 9:                            # REL32, REL32_1..5
                k = typ - 4
                A = struct.unpack_from('<i', buf, rva)[0]
                v = S + A - (P + 4 + k)
                assert -(1 << 31) <= v < (1 << 31), (sec['name'], hex(S), hex(P))
                struct.pack_into('<i', buf, rva, v)
                if record and not is_internal(S):
                    fixups.append((P, 'rel32_out'))
            else:
                raise ValueError('unsupported reloc type %d in %s' % (typ, sec['name']))

    for s in code_secs + ro_secs + data_secs:
        apply(s, addr[s['idx']], True)
    for s in pdata_secs:
        apply(s, 0, False)
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
        b = s['data']
        for i in range(0, len(b), 12):
            pdata.append(struct.unpack_from('<III', b, i))
    syms = {}
    for si, sym in o.symbols.items():
        if sym['sec'] > 0 and sym['sec'] in addr and sym['cls'] in (2, 3) and sym['name']:
            syms.setdefault(sym['name'], addr[sym['sec']] + sym['value'])
    return dict(code=code, data=data, pdata=pdata, symbols=syms, fixups=fixups)


class Package:
    def __init__(self, feature, exe_key, obj, B):
        self.feature, self.exe_key = feature, exe_key
        ex = EXES[exe_key]
        self.exe_path = ex['path']
        self.pe = pe = PEFile(open(ex['path'], 'rb').read())
        self.cfg = split_cfg(json.load(open(ex['cfg'])))[feature]
        self.obj = obj
        self.B = B
        self.build(B)

    # ------------------------------------------------------------------ blob
    def build(self, B):
        pe, cfg = self.pe, self.cfg
        symmap = {k: int(v, 16) for k, v in cfg['symbols'].items()}
        symmap['_fltused'] = 0
        clones = [Clone(pe, self.exe_path, c) for c in cfg['clones']]
        clone_va = {}
        cur = B
        for c in clones:
            clone_va[c.sym] = cur
            symmap[c.sym] = cur
            cur = align(cur + c.size, 16)
        code_va = cur
        # sizes first (layout does not depend on the addresses)
        probe = link_blob(self.obj, code_va, code_va + 0x4000000, B, dict(symmap), lambda a: True)
        code_size = len(probe['code'])
        unwind_off = align(code_va - B + code_size, 16)
        unwind_total = sum(align(c.unwind_size, 4) for c in clones)
        pdata_off = align(unwind_off + unwind_total, 16)
        n_pdata = len(probe['pdata']) + sum(len(c.pdata) for c in clones)
        a_size = pdata_off + 12 * n_pdata
        data_off = align(a_size, 0x1000)
        data_size = len(probe['data'])
        total = data_off + max(align(data_size, 0x1000), 0x1000)

        def is_internal(a):
            return B <= a < B + total
        res = link_blob(self.obj, code_va, B + data_off, B, symmap, is_internal)
        assert len(res['code']) == code_size and len(res['data']) == data_size
        code, data = res['code'], res['data']
        syms = dict(res['symbols'])
        for c in clones:
            syms[c.sym] = clone_va[c.sym]
        allsyms = dict(symmap)
        allsyms.update(syms)
        fixups = [(va - B, k) for va, k in res['fixups']]
        checks = []

        # vtables inherited from engine classes (engine addresses: constants)
        for vf in cfg['vtable_fills']:
            off = syms[vf['sym']] - code_va
            base_va, n = H(vf['base']), vf['n']
            for i in range(n):
                if u64(code, off + 8 * i) == 0:
                    struct.pack_into('<Q', code, off + 8 * i, u64(pe.d, pe.va2off(base_va + 8 * i)))
            checks.append((base_va, 8 * n, 'vtable copied into %s' % vf['sym']))
        for dp in cfg['data_patches']:
            off = syms[dp['sym']] - (B + data_off)
            for fmt, val in dp['values']:
                assert isinstance(val, int) or not val.startswith(('MPM', 'SRA', 'CS', 'g_'))
                struct.pack_into(fmt, data, off, H(val) if isinstance(val, str) else val)
                off += struct.calcsize(fmt)

        blob = bytearray(total)
        clone_pdata = []
        uw_va = B + unwind_off
        self.report = []
        for c in clones:
            cf = []
            cbytes, ubytes, pd, rep = c.build(clone_va[c.sym], allsyms, uw_va, rva_base=B, fixups=cf,
                                              is_internal=is_internal)
            o = clone_va[c.sym] - B
            blob[o:o + len(cbytes)] = cbytes
            blob[uw_va - B:uw_va - B + len(ubytes)] = ubytes
            uw_va += align(len(ubytes), 4)
            clone_pdata += pd
            fixups += [(o + fo, k) for fo, k in cf]
            checks.append((c.start, c.size, 'function copied to %s' % c.sym))
            self.report += rep
        blob[code_va - B:code_va - B + code_size] = code
        pdata = sorted(clone_pdata + list(res['pdata']))
        for i, (b, e, u) in enumerate(pdata):
            assert b < e <= unwind_off + unwind_total and u < a_size, 'pdata entry out of range'
            if i:
                assert pdata[i - 1][1] <= b, 'overlapping pdata entries'
            struct.pack_into('<III', blob, pdata_off + 12 * i, b, e, u)
        blob[data_off:data_off + data_size] = data

        # fields must not overlap
        fixups.sort()
        width = {'self64': 8, 'rel32_out': 4, 'imgrel32': 4}
        for (o1, k1), (o2, k2) in zip(fixups, fixups[1:]):
            assert o1 + width[k1] <= o2, 'overlapping fixups at %#x' % o1
        assert all(o + width[k] <= a_size or data_off <= o for o, k in fixups)

        # patches to the executable
        patches = []
        for e in cfg['vtable_patches']:
            at, expect = H(e['at']), H(e['expect'])
            assert u64(pe.d, pe.va2off(at)) == expect, 'vtable entry %#x' % at
            patches.append(dict(kind=PATCH_ABS64, site=at, expect=expect, to=e['to'], opcode=0, what=e.get('what', '')))
        for e in cfg['call_patches']:
            at, expect, opc = H(e['at']), H(e['expect']), H(e['opcode'])
            o = pe.va2off(at)
            assert pe.d[o] == opc and at + 5 + struct.unpack_from('<i', pe.d, o + 1)[0] == expect, 'branch at %#x' % at
            patches.append(dict(kind=PATCH_REL32, site=at, expect=expect, to=e['to'], opcode=opc, what=e.get('what', '')))
        for e in cfg['sinit_hooks']:
            at = H(e['at'])
            o = pe.va2off(at)
            assert pe.d[o] == 0xe8 and pe.d[o - 7:o - 4] == b'\x48\x8d\x0d', 'static initializer call at %#x' % at
            expect = at + 5 + struct.unpack_from('<i', pe.d, o + 1)[0]
            assert expect == symmap['eng_meta_register']
            patches.append(dict(kind=PATCH_REL32, site=at, expect=expect, to=e['to'], opcode=0xe8,
                                what='static initializer: ' + e.get('what', '')))
            checks.append((at - 7, 12, 'static initializer ' + e.get('what', '')))
        for p in patches:
            p['target'] = syms[p['to']]
            assert is_internal(p['target'])
        sites = sorted((p['site'], 8 if p['kind'] == PATCH_ABS64 else 5) for p in patches)
        for (a1, l1), (a2, l2) in zip(sites, sites[1:]):
            assert a1 + l1 <= a2, 'overlapping patch sites %#x' % a1

        self.blob, self.fixups, self.patches, self.checks = bytes(blob), fixups, patches, checks
        self.syms = syms
        self.layout = dict(total=total, code_size=data_off, a_size=a_size, data_off=data_off, data_size=data_size,
                           pdata_off=pdata_off, n_pdata=len(pdata), code_va_off=code_va - B, unwind_off=unwind_off)
        self.needs_pre_crt = bool(cfg['sinit_hooks'])

    # ------------------------------------------------------------------ helpers
    @staticmethod
    def apply_fixups(blob, fixups, delta):
        b = bytearray(blob)
        for o, k in fixups:
            if k == 'self64':
                struct.pack_into('<Q', b, o, (u64(b, o) + delta) & 0xffffffffffffffff)
            elif k == 'rel32_out':
                v = struct.unpack_from('<i', b, o)[0] - delta
                assert -(1 << 31) <= v < 1 << 31
                struct.pack_into('<i', b, o, v)
            else:
                v = u32(b, o) + delta
                assert 0 <= v < 1 << 31
                struct.pack_into('<I', b, o, v)
        return bytes(b)

    def identity(self):
        pe = self.pe
        ts = u32(pe.d, pe.fh + 4)
        cks = u32(pe.d, pe.oh + 64)
        lc_rva, lc_size = pe.dir(10)
        cookie_va = u64(pe.d, pe.rva2off(lc_rva) + 0x58)
        cookie_default = u64(pe.d, pe.va2off(cookie_va))
        assert cookie_default == 0x2B992DDFA232, hex(cookie_default)
        return ts, pe.size_of_image, cks, cookie_va, cookie_default

    def serialize(self):
        pe = self.pe
        ts, soi, cks, cookie_va, cookie_default = self.identity()
        fix = b''.join(struct.pack('<I', (o << 2) | FIX_KIND[k]) for o, k in self.fixups)
        pat = b''
        for p in self.patches:
            if p['kind'] == PATCH_BYTES:
                old, new = p['old'], p['new']
                assert len(old) == len(new) and 1 <= len(old) <= 8
                pat += struct.pack('<QQIBBHQ', p['site'], int.from_bytes(old, 'little'), 0,
                                   PATCH_BYTES, len(old), 0, int.from_bytes(new, 'little'))
            else:
                pat += struct.pack('<QQIBBHQ', p['site'], p['expect'], p['target'] - self.B, p['kind'], p['opcode'], 0, 0)
        chk = b''
        for va, n, what in self.checks:
            o = pe.va2off(va)
            chk += struct.pack('<QIIQ', va, n, 0, fnv64(pe.d[o:o + n]))
        L = self.layout
        off_blob = HDR_SIZE
        off_fix = off_blob + len(self.blob)
        off_patch = align(off_fix + len(fix), 8)
        off_check = off_patch + len(pat)
        end = off_check + len(chk)
        hdr = struct.pack('<II24sIIII', MAGIC, VERSION, self.feature.encode(), ts, soi, cks,
                          FLAG_PRE_CRT if self.needs_pre_crt else 0)
        hdr += struct.pack('<QQQQ', pe.image_base, self.B, cookie_va, cookie_default)
        hdr += struct.pack('<IIIIIIIII', L['total'], L['code_size'], L['pdata_off'], L['n_pdata'],
                           len(self.fixups), len(self.patches), len(self.checks), off_blob, off_fix)
        hdr += struct.pack('<II', off_patch, off_check)
        hdr += EXES[self.exe_key]['name'].encode().ljust(40, b'\0')
        hdr += struct.pack('<I', end)
        assert len(hdr) <= HDR_SIZE, len(hdr)
        hdr = hdr.ljust(HDR_SIZE, b'\0')
        out = bytearray(hdr) + self.blob + fix
        out += b'\0' * (off_patch - len(out)) + pat + chk
        assert len(out) == end
        return bytes(out)


class BytePatchPackage(Package):
    """a package with no image of its own: just raw byte patches to the executable, each with its expected prior bytes"""

    def __init__(self, feature, exe_key, patchlist):
        self.feature, self.exe_key = feature, exe_key
        self.pe = pe = PEFile(open(EXES[exe_key]['path'], 'rb').read())
        self.B = pe.image_base
        self.blob, self.fixups, self.syms = b'', [], {}
        self.needs_pre_crt = False
        patches, checks = [], []
        for va, old, new in patchlist:
            o = pe.va2off(va)
            assert bytes(pe.d[o:o + len(old)]) == old, 'byte patch %#x: have %s, expected %s' % (
                va, bytes(pe.d[o:o + len(old)]).hex(), old.hex())
            patches.append(dict(kind=PATCH_BYTES, site=va, old=old, new=new, what='byte patch %#x' % va))
            checks.append((va, len(old), 'byte patch context %#x' % va))
        sites = sorted((p['site'], len(p['old'])) for p in patches)
        for (a1, l1), (a2, l2) in zip(sites, sites[1:]):
            assert a1 + l1 <= a2, 'overlapping byte patches at %#x' % a1
        self.patches, self.checks = patches, checks
        self.layout = dict(total=0, code_size=0, a_size=0, data_off=0, data_size=0, pdata_off=0, n_pdata=0,
                           code_va_off=0, unwind_off=0)


def nominal_base(exe_key, feature):
    pe = PEFile(open(EXES[exe_key]['path'], 'rb').read())
    lo = align(pe.image_base + pe.size_of_image, 0x10000)
    return lo + FEATURES[feature]['slot'] * SLOT_SIZE


def main():
    outdir = sys.argv[1]
    only = sys.argv[2] if len(sys.argv) > 2 else None   # optional: build just this one feature
    os.makedirs(os.path.join(outdir, 'obj'), exist_ok=True)
    summary = {}
    for feature, fd in FEATURES.items():
        if only and feature != only:
            continue
        obj = compile_obj(feature, os.path.join(outdir, 'obj'))
        pk = {}
        for exe_key in fd['exes']:
            B = nominal_base(exe_key, feature)
            p1 = Package(feature, exe_key, obj, B)
            assert p1.layout['total'] <= SLOT_SIZE
            # self-check at a second address
            B2 = B + 0x5a30000
            p2 = Package(feature, exe_key, obj, B2)
            assert p1.fixups == p2.fixups, 'fixup lists differ between bases'
            assert Package.apply_fixups(p1.blob, p1.fixups, B2 - B) == p2.blob, 'fixups do not reproduce the second link'
            assert Package.apply_fixups(p2.blob, p2.fixups, B - B2) == p1.blob
            assert [(p['site'], p['target'] - B) for p in p1.patches] == [(p['site'], p['target'] - B2) for p in p2.patches]
            data = p1.serialize()
            name = '%s_%s' % (feature, exe_key)
            open(os.path.join(outdir, name + '.pkg'), 'wb').write(data)
            json.dump(dict(base=hex(B), layout=p1.layout, syms={k: hex(v - B) for k, v in p1.syms.items()},
                           patches=[dict(site=hex(p['site']), expect=hex(p['expect']), to=p['to'],
                                         target_off=hex(p['target'] - B), kind=p['kind'], what=p['what'])
                                    for p in p1.patches],
                           checks=[(hex(a), n, w) for a, n, w in p1.checks],
                           fixups={k: sum(1 for o, kk in p1.fixups if kk == k) for k in FIX_KIND}),
                      open(os.path.join(outdir, name + '.json'), 'w'), indent=1)
            kinds = {k: sum(1 for o, kk in p1.fixups if kk == k) for k in FIX_KIND}
            print('%-22s %-6s base %#x size %#x (code %#x, data %#x) pdata %d fixups %s patches %d checks %d pkg %d bytes'
                  % (feature, exe_key, B, p1.layout['total'], p1.layout['a_size'], p1.layout['data_size'],
                     p1.layout['n_pdata'], kinds, len(p1.patches), len(p1.checks), len(data)))
            for r in p1.report:
                print('   ' + r.strip())
            pk[exe_key] = (name, p1)
        summary[feature] = pk
    # byte-patch features (no image of their own)
    for feature, fd in BYTE_FEATURES.items():
        if only and feature != only:
            continue
        pk = {}
        for exe_key in fd['exes']:
            p1 = BytePatchPackage(feature, exe_key, fd['patches'][exe_key])
            data = p1.serialize()
            name = '%s_%s' % (feature, exe_key)
            open(os.path.join(outdir, name + '.pkg'), 'wb').write(data)
            json.dump(dict(patches=[dict(site=hex(p['site']), old=p['old'].hex(), new=p['new'].hex(), what=p['what'])
                                    for p in p1.patches],
                           checks=[(hex(a), n, w) for a, n, w in p1.checks]),
                      open(os.path.join(outdir, name + '.json'), 'w'), indent=1)
            print('%-22s %-6s byte patches %d  pkg %d bytes' % (feature, exe_key, len(p1.patches), len(data)))
            pk[exe_key] = (name, p1)
        summary[feature] = pk

    def plen(p):
        return 8 if p['kind'] == PATCH_ABS64 else len(p['old']) if p['kind'] == PATCH_BYTES else 5
    # no feature may check or patch what another feature patches
    for exe_key in EXES:
        items = [(f, pk[exe_key][1]) for f, pk in summary.items() if exe_key in pk]
        for f1, p1 in items:
            for f2, p2 in items:
                if f1 == f2:
                    continue
                for p in p2.patches:
                    n = plen(p)
                    for q in p1.patches:
                        m = plen(q)
                        assert q['site'] + m <= p['site'] or p['site'] + n <= q['site'], 'patch conflict %s/%s' % (f1, f2)
                    for va, sz, what in p1.checks:
                        assert va + sz <= p['site'] or p['site'] + n <= va, '%s checks %s patched by %s' % (f1, what, f2)
    # C headers for the ASI sources
    for feature, pk in summary.items():
        L = ['/* generated by build_pkg.py: patch packages of %s */' % feature]
        names = []
        for exe_key, (name, p) in pk.items():
            data = open(os.path.join(outdir, name + '.pkg'), 'rb').read()
            L.append('static const unsigned char pkg_%s[%d] = {' % (name, len(data)))
            for i in range(0, len(data), 32):
                L.append(','.join(str(x) for x in data[i:i + 32]) + ',')
            L.append('};')
            names.append(name)
        L.append('#define FEATURE_NAME L"%s"' % feature)
        L.append('static const unsigned char *const g_packages[] = {%s};' % ', '.join('pkg_' + n for n in names))
        L.append('#define N_PACKAGES %d' % len(names))
        open(os.path.join(outdir, 'pkg_%s.h' % feature), 'w').write('\n'.join(L) + '\n')
    print('ok')


if __name__ == '__main__':
    main()
