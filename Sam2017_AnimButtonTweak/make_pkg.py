#!/usr/bin/env python3
"""make_pkg.py - self-contained builder for a BYTE-PATCH ASI package.

Produces the pkg_<Feature>.h header that asi_plugin.c embeds (-DPKG_HEADER). A byte-patch package has no code of its
own: it just records, for one target executable, a set of (address, original bytes -> new bytes) patches, each with
the original bytes as a precondition and a hash of the surrounding bytes, plus the exe's identity so the plugin only
touches the exact executable it was built for. This is the same on-disk format the full build_pkg.py emits for a
byte-patch feature, reproduced here with no external dependencies so a byte-patch plugin builds on its own.

The header can hold one package per executable build: every --exe starts a new package, and the --patch options after
it belong to that executable. At run time the plugin applies the package whose identity matches the running program.

usage:
  make_pkg.py --feature NAME --name EXE_FILENAME --out pkg_NAME.h \
              --exe PATH --patch VA OLDHEX NEWHEX [--patch VA OLDHEX NEWHEX ...] \
             [--exe PATH2 --patch VA OLDHEX NEWHEX [...]]

  VA       target virtual address, hex (e.g. 0x1407e5900)
  OLDHEX   the bytes that must be there now, hex (e.g. 66  or  40574883ec20)
  NEWHEX   the bytes to write, same length as OLDHEX (e.g. 62  or  b801000000c3)
"""
import sys, struct

MAGIC = 0x41373153          # 'S17A'
VERSION = 1
HDR_SIZE = 192
PATCH_BYTES = 3
COOKIE_DEFAULT_EXPECTED = 0x2B992DDFA232


def u16(d, o): return struct.unpack_from('<H', d, o)[0]
def u32(d, o): return struct.unpack_from('<I', d, o)[0]
def u64(d, o): return struct.unpack_from('<Q', d, o)[0]


def fnv64(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h


class MiniPE:
    """just enough of a PE32+ reader for the package header"""
    def __init__(self, data):
        self.d = data
        lfanew = u32(data, 0x3c)
        assert u32(data, lfanew) == 0x00004550, 'not a PE file'
        self.fh = lfanew + 4                      # IMAGE_FILE_HEADER
        self.oh = lfanew + 24                     # IMAGE_OPTIONAL_HEADER
        assert u16(data, self.oh) == 0x20b, 'not PE32+'
        self.image_base = u64(data, self.oh + 24)
        self.size_of_image = u32(data, self.oh + 56)
        nsec = u16(data, self.fh + 2)
        opt_sz = u16(data, self.fh + 16)
        sh = self.oh + opt_sz
        self.secs = []
        for i in range(nsec):
            s = sh + 40 * i
            vsize, va, rsize, rptr = struct.unpack_from('<IIII', data, s + 8)
            self.secs.append((va, vsize, rptr, rsize))

    def dir(self, n):                             # data directory n -> (rva, size)
        return struct.unpack_from('<II', self.d, self.oh + 112 + 8 * n)

    def rva2off(self, rva):
        for va, vsize, rptr, rsize in self.secs:
            if va <= rva < va + max(vsize, rsize):
                return rptr + (rva - va)
        return rva                                # headers: file offset == rva

    def va2off(self, va):
        return self.rva2off(va - self.image_base)


def build_package(feature, exe_path, exe_name, patches):
    pe = MiniPE(open(exe_path, 'rb').read())
    ts = u32(pe.d, pe.fh + 4)
    cks = u32(pe.d, pe.oh + 64)
    soi = pe.size_of_image
    lc_rva, _ = pe.dir(10)                         # load config directory
    cookie_va = u64(pe.d, pe.rva2off(lc_rva) + 0x58)
    cookie_default = u64(pe.d, pe.va2off(cookie_va))
    assert cookie_default == COOKIE_DEFAULT_EXPECTED, \
        'unexpected security cookie %#x - is this the right executable?' % cookie_default

    # verify preconditions + no overlap
    norm = []
    for va, old, new in patches:
        assert len(old) == len(new) and 1 <= len(old) <= 8, 'patch at %#x: bad length' % va
        have = bytes(pe.d[pe.va2off(va):pe.va2off(va) + len(old)])
        assert have == old, 'byte patch %#x: have %s, expected %s' % (va, have.hex(), old.hex())
        norm.append((va, old, new))
    s = sorted((va, len(old)) for va, old, _ in norm)
    for (a1, l1), (a2, l2) in zip(s, s[1:]):
        assert a1 + l1 <= a2, 'overlapping byte patches at %#x' % a1

    # records (identical layout to build_pkg.py's serialize for PATCH_BYTES)
    pat = b''.join(struct.pack('<QQIBBHQ', va, int.from_bytes(old, 'little'), 0,
                               PATCH_BYTES, len(old), 0, int.from_bytes(new, 'little'))
                   for va, old, new in norm)
    chk = b''.join(struct.pack('<QIIQ', va, len(old), 0, fnv64(pe.d[pe.va2off(va):pe.va2off(va) + len(old)]))
                   for va, old, new in norm)

    off_blob = HDR_SIZE
    off_fix = off_blob                             # no blob
    off_patch = (off_fix + 7) & ~7                 # align 8 (no fixups)
    off_check = off_patch + len(pat)
    end = off_check + len(chk)

    hdr = struct.pack('<II24sIIII', MAGIC, VERSION, feature.encode(), ts, soi, cks, 0)   # flags 0 (not pre-CRT)
    hdr += struct.pack('<QQQQ', pe.image_base, pe.image_base, cookie_va, cookie_default)  # nominal == image base
    hdr += struct.pack('<IIIIIIIII', 0, 0, 0, 0, 0, len(norm), len(norm), off_blob, off_fix)
    hdr += struct.pack('<II', off_patch, off_check)
    hdr += exe_name.encode().ljust(40, b'\0')
    hdr += struct.pack('<I', end)
    assert len(hdr) <= HDR_SIZE
    hdr = hdr.ljust(HDR_SIZE, b'\0')

    out = bytearray(hdr)
    out += b'\0' * (off_patch - len(out)) + pat + chk
    assert len(out) == end
    return bytes(out)


def emit_header(feature, exe_name, datas, path):
    base = 'pkg_%s_%s' % (feature, exe_name.replace('.exe', '').replace('.', '_'))
    L = ['/* generated by make_pkg.py: byte-patch package(s) of %s */' % feature]
    arrs = []
    for k, data in enumerate(datas):
        arr = base if k == 0 else '%s_%d' % (base, k + 1)
        arrs.append(arr)
        L.append('static const unsigned char %s[%d] = {' % (arr, len(data)))
        for i in range(0, len(data), 32):
            L.append(','.join(str(x) for x in data[i:i + 32]) + ',')
        L.append('};')
    L.append('#define FEATURE_NAME L"%s"' % feature)
    L.append('static const unsigned char *const g_packages[] = {%s};' % ', '.join(arrs))
    L.append('#define N_PACKAGES %d' % len(arrs))
    open(path, 'w').write('\n'.join(L) + '\n')


def parse_args(argv):
    """--feature/--name/--out once; then groups:  --exe PATH  --patch VA OLD NEW  [--patch ...]"""
    opts, groups = {}, []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('--feature', '--name', '--out'):
            opts[a[2:]] = argv[i + 1]
            i += 2
        elif a == '--exe':
            groups.append([argv[i + 1], []])
            i += 2
        elif a == '--patch':
            if not groups:
                raise SystemExit('--patch before --exe')
            groups[-1][1].append((int(argv[i + 1], 16), bytes.fromhex(argv[i + 2]), bytes.fromhex(argv[i + 3])))
            i += 4
        else:
            raise SystemExit('unknown argument %s' % a)
    for k in ('feature', 'name', 'out'):
        if k not in opts:
            raise SystemExit('missing --%s' % k)
    if not groups or any(not g[1] for g in groups):
        raise SystemExit('every --exe needs at least one --patch')
    return opts, groups


def main():
    opts, groups = parse_args(sys.argv[1:])
    datas = []
    for exe, patches in groups:
        data = build_package(opts['feature'], exe, opts['name'], patches)
        datas.append(data)
        print('%s: %d-byte package for %s, %d patch(es)' % (opts['feature'], len(data), exe, len(patches)))
    emit_header(opts['feature'], opts['name'], datas, opts['out'])
    print('%s: %d package(s) -> %s' % (opts['feature'], len(datas), opts['out']))


if __name__ == '__main__':
    main()
