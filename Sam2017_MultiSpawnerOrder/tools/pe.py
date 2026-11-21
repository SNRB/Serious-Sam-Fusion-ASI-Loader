"""Minimal PE reader for analysis (no external deps)."""
import struct, mmap, re, os, subprocess

class PE:
    def __init__(self, path):
        self.path = path
        f = open(path, 'rb')
        self.data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        d = self.data
        e_lfanew = struct.unpack_from('<I', d, 0x3c)[0]
        self.nt = e_lfanew
        self.nsec = struct.unpack_from('<H', d, e_lfanew + 6)[0]
        self.timestamp = struct.unpack_from('<I', d, e_lfanew + 8)[0]
        opt_size = struct.unpack_from('<H', d, e_lfanew + 20)[0]
        opt = e_lfanew + 24
        self.opt = opt
        self.imagebase = struct.unpack_from('<Q', d, opt + 24)[0]
        self.entry = struct.unpack_from('<I', d, opt + 16)[0]
        self.sizeofimage = struct.unpack_from('<I', d, opt + 56)[0]
        self.checksum = struct.unpack_from('<I', d, opt + 64)[0]
        ndirs = struct.unpack_from('<I', d, opt + 108)[0]
        self.dirs = [struct.unpack_from('<II', d, opt + 112 + 8 * i) for i in range(ndirs)]
        sec = opt + opt_size
        self.sections = []
        for i in range(self.nsec):
            name = d[sec:sec + 8].rstrip(b'\0').decode('latin1')
            vsize, va, rawsize, rawptr = struct.unpack_from('<IIII', d, sec + 8)
            ch = struct.unpack_from('<I', d, sec + 36)[0]
            self.sections.append((name, va, vsize, rawptr, rawsize, ch))
            sec += 40

    def sec(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        raise KeyError(name)

    def rva2off(self, rva):
        for name, va, vsize, rawptr, rawsize, ch in self.sections:
            if va <= rva < va + max(vsize, rawsize):
                if rva - va >= rawsize:
                    return None
                return rawptr + rva - va
        return None

    def off2rva(self, off):
        for name, va, vsize, rawptr, rawsize, ch in self.sections:
            if rawptr <= off < rawptr + rawsize:
                return va + off - rawptr
        return None

    def va2off(self, va):
        return self.rva2off(va - self.imagebase)

    def read(self, va, n):
        o = self.va2off(va)
        return bytes(self.data[o:o + n])

    def u8(self, va): return self.read(va, 1)[0]
    def u16(self, va): return struct.unpack('<H', self.read(va, 2))[0]
    def u32(self, va): return struct.unpack('<I', self.read(va, 4))[0]
    def i32(self, va): return struct.unpack('<i', self.read(va, 4))[0]
    def u64(self, va): return struct.unpack('<Q', self.read(va, 8))[0]
    def f32(self, va): return struct.unpack('<f', self.read(va, 4))[0]

    def cstr(self, va, maxlen=512):
        o = self.va2off(va)
        if o is None:
            return None
        e = self.data.find(b'\0', o, o + maxlen)
        if e < 0:
            e = o + maxlen
        return bytes(self.data[o:e]).decode('latin1')

    def find_all(self, pat, sec=None):
        """Yield VAs of byte pattern occurrences (optionally within section)."""
        d = self.data
        if sec:
            s = self.sec(sec)
            start, end = s[3], s[3] + s[4]
        else:
            start, end = 0, len(d)
        i = d.find(pat, start, end)
        while i >= 0:
            rva = self.off2rva(i)
            if rva is not None:
                yield self.imagebase + rva
            i = d.find(pat, i + 1, end)

    def find_cstr(self, s):
        """VAs of exact NUL-terminated string s (bytes or str), preceded by NUL."""
        if isinstance(s, str):
            s = s.encode('latin1')
        out = []
        for va in self.find_all(s + b'\0'):
            o = self.va2off(va)
            if o > 0 and self.data[o - 1] == 0:
                out.append(va)
        return out

    def text_range(self):
        s = self.sec('.text')
        return self.imagebase + s[1], self.imagebase + s[1] + s[2]

    # --- RIP-relative reference scanning -----------------------------------
    _ripcache = None

    def rip_refs(self, target):
        """Find code locations whose disp32 (RIP-relative, assuming the disp
        ends the instruction or is followed by imm of 0/1/4 bytes) hits target.
        Returns list of (disp_va, extra) where extra = trailing imm size."""
        s = self.sec('.text')
        base = self.imagebase + s[1]
        raw = self.data[s[3]:s[3] + s[4]]
        out = []
        n = len(raw)
        # For each possible extra (0,1,4), disp = target - (disp_va + 4 + extra)
        # Scan via numpy-free approach: iterate windows using struct on chunks
        import array
        for extra in (0, 1, 2, 4):
            pass
        return out


def disasm(pe_path, start, end, intel=True):
    args = ['objdump', '-d', '--no-show-raw-insn', '-M', 'intel' if intel else 'att',
            '--start-address=0x%x' % start, '--stop-address=0x%x' % end, pe_path]
    r = subprocess.run(args, capture_output=True, text=True)
    lines = r.stdout.splitlines()
    out = []
    started = False
    for l in lines:
        if re.match(r'^\s+[0-9a-f]+:', l):
            started = True
            out.append(l)
    return '\n'.join(out)
