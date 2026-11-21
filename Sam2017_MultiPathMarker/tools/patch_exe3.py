"""PE32+ reader used by the package builder (build_pkg.py) and patch_exe5.py.

Only what the builders need: headers, section table, data directories and RVA/VA <-> file offset mapping of the
ORIGINAL executables (Sam2017_SeriousEditor.exe / Sam2017_Unrestricted.exe, 7 June 2019).

    pe = PEFile(open(path, 'rb').read())
    pe.d                bytes of the file
    pe.fh, pe.oh        file offsets of IMAGE_FILE_HEADER and IMAGE_OPTIONAL_HEADER64
    pe.image_base, pe.size_of_image, pe.sections
    pe.dir(i)           (rva, size) of data directory i
    pe.rva2off(rva), pe.va2off(va), pe.off2va(off), pe.read(va, n)
"""
import struct


def u8(b, o):
    return b[o]


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def s32(b, o):
    return struct.unpack_from('<i', b, o)[0]


def u64(b, o):
    return struct.unpack_from('<Q', b, o)[0]


IMAGE_SCN_CNT_CODE = 0x20
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_WRITE = 0x80000000


class Section:
    def __init__(self, name, va, vsize, raw, rsize, ch):
        self.name, self.va, self.vsize, self.raw, self.rsize, self.ch = name, va, vsize, raw, rsize, ch

    def __repr__(self):
        return 'Section(%s rva %#x vsize %#x raw %#x rsize %#x ch %#x)' % (
            self.name, self.va, self.vsize, self.raw, self.rsize, self.ch)


class PEFile:
    def __init__(self, data):
        self.d = d = data
        assert d[:2] == b'MZ', 'not an MZ file'
        pe = u32(d, 0x3c)
        assert d[pe:pe + 4] == b'PE\0\0', 'not a PE file'
        self.fh = fh = pe + 4
        self.machine = u16(d, fh)
        self.nsec = u16(d, fh + 2)
        opt_size = u16(d, fh + 16)
        self.oh = oh = fh + 20
        assert u16(d, oh) == 0x20b, 'not PE32+'
        self.entry = u32(d, oh + 16)
        self.image_base = u64(d, oh + 24)
        self.section_alignment = u32(d, oh + 32)
        self.file_alignment = u32(d, oh + 36)
        self.size_of_image = u32(d, oh + 56)
        self.size_of_headers = u32(d, oh + 60)
        self.ndirs = u32(d, oh + 108)
        self.dirs = oh + 112
        self.sh = oh + opt_size
        self.sections = []
        for i in range(self.nsec):
            o = self.sh + 40 * i
            name = d[o:o + 8].rstrip(b'\0').decode('latin-1')
            vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o + 8)
            ch = u32(d, o + 36)
            self.sections.append(Section(name, va, vsize, raw, rsize, ch))

    def dir(self, i):
        return u32(self.d, self.dirs + 8 * i), u32(self.d, self.dirs + 8 * i + 4)

    def section_of(self, rva):
        for s in self.sections:
            if s.va <= rva < s.va + max(s.vsize, s.rsize):
                return s
        return None

    def rva2off(self, rva):
        if rva < self.size_of_headers:
            return rva
        s = self.section_of(rva)
        if s is None or rva - s.va >= s.rsize:
            raise ValueError('RVA %#x is not backed by file data' % rva)
        return s.raw + rva - s.va

    def va2off(self, va):
        return self.rva2off(va - self.image_base)

    def off2va(self, off):
        for s in self.sections:
            if s.raw <= off < s.raw + s.rsize:
                return self.image_base + s.va + off - s.raw
        raise ValueError('file offset %#x is not in a section' % off)

    def read(self, va, n):
        o = self.va2off(va)
        return self.d[o:o + n]

    def is_code(self, va):
        s = self.section_of(va - self.image_base)
        return bool(s and s.ch & IMAGE_SCN_MEM_EXECUTE)

    # the exception directory (.pdata): sorted RUNTIME_FUNCTION entries (begin, end, unwind) as RVAs
    def runtime_functions(self):
        if not hasattr(self, '_rf'):
            rva, size = self.dir(3)
            o = self.rva2off(rva)
            self._rf = [struct.unpack_from('<III', self.d, o + 12 * i) for i in range(size // 12)]
        return self._rf

    def function_entry(self, va):
        """the RUNTIME_FUNCTION that covers va (binary search), or None"""
        import bisect
        rf = self.runtime_functions()
        rva = va - self.image_base
        i = bisect.bisect_right(rf, (rva, 0xffffffff, 0xffffffff)) - 1
        if i >= 0 and rf[i][0] <= rva < rf[i][1]:
            return rf[i]
        return None
