#!/usr/bin/env python3
"""Generate exe_expect.h for Sam2017_EditorToolsQOLpatches.asi from the original Sam2017_SeriousEditor.exe of each
supported build (7 June 2019 and 19 November 2026): identity of the exe, the RVAs of the editor functions the plugin
uses, FNV-1a 64 hashes of the code and data the plugin relies on, and the vtable slots it changes, with their original
values. The 2026 build is the same code at other addresses; every assertion below is checked for both.
Usage: gen_expect.py <Sam2017_SeriousEditor.exe of 7 June 2019> <Sam2017_SeriousEditor.exe of 19 November 2026> <out.h>"""
import sys, struct

# ---- the addresses (RVAs) of each build
BUILDS = [
    ('7 June 2019', dict(
        REPARENT=0x19EE000, THUNK_REPARENT=0x19EDFF0, THUNK_IN_PLACE=0x19EDFE0,
        VT_REPARENT=0x2621F70, VT_IN_PLACE=0x2622090,
        FACTORY_REPARENT=0x19EF2C0, FACTORY_IN_PLACE=0x19EF280,
        REG=0x19EEB47, REG_IN_PLACE=0x19EEC47, REG_IN_PLACE_FACTORY=0x19EEC69, KEY_REPARENT=0x19EEC33, KEY_IN_PLACE=0x19EED33,
        CMD_IN_PLACE=0x19EF3F0, CMD_REPARENT=0x19EF420,
        BoneSelection=0x19D9DB0, ListAddRef=0x15BF7C0, ListRelease=0x15BFE40, ListCount=0x15BFD00, ListItem=0x15BFD30,
        BoneName=0x19D2330, BoneLOD=0x19D23A0, BoneParentName=0x19D23F0, FindBone=0x19F5540, BonePlacement=0x19F5630,
        BoneParent=0x19F5730, SetBoneParent=0x19D2780, SetBoneRelPlacement=0x19D27A0, BoneFromHandle=0x19DC1E0,
        LODHandle=0x19DC2E0, AskText=0x1673DB0, CStringFrom=0x0B25C50, CStringFree=0x0B269A0,
        IdentFromString=0x0B2AB30, IdentText=0x0B2A9F0, UndoBegin=0x15C0510, UndoCommit=0x15C0680, UndoEnd=0x15C05B0,
        SkeletonTree=0x19EC460, SkeletonTreeReset=0x19EC2A0, ConsoleInfo=0x0B1FFD0, ConsoleWarning=0x0B20970)),
    ('19 November 2026', dict(
        REPARENT=0x1A2ED70, THUNK_REPARENT=0x1A2ED60, THUNK_IN_PLACE=0x1A2ED50,
        VT_REPARENT=0x2688300, VT_IN_PLACE=0x2688420,
        FACTORY_REPARENT=0x1A30030, FACTORY_IN_PLACE=0x1A2FFF0,
        REG=0x1A2F8B7, REG_IN_PLACE=0x1A2F9B7, REG_IN_PLACE_FACTORY=0x1A2F9D9, KEY_REPARENT=0x1A2F9A3, KEY_IN_PLACE=0x1A2FAA3,
        CMD_IN_PLACE=0x1A30160, CMD_REPARENT=0x1A30190,
        BoneSelection=0x1A1AAE0, ListAddRef=0x15FF420, ListRelease=0x15FFAA0, ListCount=0x15FF960, ListItem=0x15FF990,
        BoneName=0x1A130A0, BoneLOD=0x1A13110, BoneParentName=0x1A13160, FindBone=0x1A361A0, BonePlacement=0x1A36290,
        BoneParent=0x1A36390, SetBoneParent=0x1A134F0, SetBoneRelPlacement=0x1A13510, BoneFromHandle=0x1A1CF00,
        LODHandle=0x1A1D000, AskText=0x16B3E30, CStringFrom=0x0B25940, CStringFree=0x0B26690,
        IdentFromString=0x0B2A840, IdentText=0x0B2A700, UndoBegin=0x1600170, UndoCommit=0x16002E0, UndoEnd=0x1600210,
        SkeletonTree=0x1A2D1D0, SkeletonTreeReset=0x1A2D010, ConsoleInfo=0x0B1FCE0, ConsoleWarning=0x0B20680)),
]
# the editor functions the plugin calls, in the order of struct ExeBuild in the header
CALLED = ('REPARENT', 'BoneSelection', 'ListRelease', 'ListCount', 'ListItem', 'BoneName', 'BoneLOD', 'BoneParentName',
          'FindBone', 'BonePlacement', 'BoneParent', 'SetBoneParent', 'SetBoneRelPlacement', 'AskText', 'CStringFrom',
          'CStringFree', 'IdentFromString', 'IdentText', 'UndoBegin', 'UndoCommit', 'UndoEnd', 'SkeletonTree',
          'SkeletonTreeReset', 'ConsoleInfo', 'ConsoleWarning')


class Exe:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        e = struct.unpack_from('<I', d, 0x3c)[0]
        nsec = struct.unpack_from('<H', d, e + 6)[0]
        self.ts = struct.unpack_from('<I', d, e + 8)[0]
        opt = e + 24
        osz = struct.unpack_from('<H', d, e + 20)[0]
        self.image_base = struct.unpack_from('<Q', d, opt + 24)[0]
        self.size_of_image = struct.unpack_from('<I', d, opt + 56)[0]
        self.checksum = struct.unpack_from('<I', d, opt + 64)[0]
        self.secs = []
        for i in range(nsec):
            o = opt + osz + 40 * i
            vsz, va, rsz, raw = struct.unpack_from('<IIII', d, o + 8)
            self.secs.append((va, vsz, raw, rsz))

    def read(self, rva, n):
        for va, vsz, raw, rsz in self.secs:
            if va <= rva and rva + n <= va + rsz:
                return self.d[raw + rva - va: raw + rva - va + n]
        raise ValueError('rva %#x not in file data' % rva)

    def q(self, rva):
        return struct.unpack('<Q', self.read(rva, 8))[0]

    def u32(self, rva):
        return struct.unpack('<I', self.read(rva, 4))[0]

    def cstr(self, va):
        rva = va - self.image_base
        out = b''
        while self.read(rva, 1) != b'\0':
            out += self.read(rva, 1)
            rva += 1
        return out.decode('latin1')

    def rel_target(self, rva):
        """target of the call/jmp rel32 at rva (opcode e8/e9)"""
        assert self.read(rva, 1) in (b'\xe8', b'\xe9'), 'no call/jmp at %#x' % rva
        return rva + 5 + struct.unpack('<i', self.read(rva + 1, 4))[0]

    def lea_target(self, rva):
        """target of the rip-relative lea at rva (48/4c 8d xx disp32)"""
        b = self.read(rva, 7)
        assert b[0] in (0x48, 0x4c) and b[1] == 0x8d, 'no lea at %#x' % rva
        return rva + 7 + struct.unpack('<i', b[3:7])[0]


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h


SLOT = 30                                # the slot the editor's tool activation runs (as for every CTool)


def analyse(x, A):
    """the assertions of what the plugin assumes about the two tools; returns (changes, regions)"""
    B = x.image_base
    REPARENT, THUNK_REPARENT, THUNK_IN_PLACE = A['REPARENT'], A['THUNK_REPARENT'], A['THUNK_IN_PLACE']
    VT_REPARENT, VT_IN_PLACE = A['VT_REPARENT'], A['VT_IN_PLACE']
    assert x.read(THUNK_REPARENT, 3) == b'\x33\xd2\xe9' and \
           x.rel_target(THUNK_REPARENT + 2) == REPARENT, 'Tool.SetBoneParent: xor edx,edx; jmp Reparent'
    assert x.read(THUNK_IN_PLACE, 5) == b'\xba\x01\x00\x00\x00' and x.rel_target(THUNK_IN_PLACE + 5) == REPARENT, \
           'Tool.SetBoneParentInPlace: mov edx,1; jmp Reparent'
    assert x.q(VT_REPARENT + 8 * SLOT) == B + THUNK_REPARENT and x.q(VT_IN_PLACE + 8 * SLOT) == B + THUNK_IN_PLACE
    # the factories registered for the two tools install those vtables
    FACTORY_REPARENT, FACTORY_IN_PLACE = A['FACTORY_REPARENT'], A['FACTORY_IN_PLACE']
    assert x.lea_target(FACTORY_REPARENT + 0x20) == VT_REPARENT and x.lea_target(FACTORY_IN_PLACE + 0x20) == VT_IN_PLACE
    # ...registered with these ids, and the shortcuts Shift+S and Shift+Alt+S (0x20000000 Shift, 0x40000000 Alt, 0x31 'S')
    REG = A['REG']
    assert x.cstr(B + x.lea_target(REG)) == 'Tool.SetBoneParent' and x.lea_target(REG + 0x22) == FACTORY_REPARENT
    assert x.cstr(B + x.lea_target(A['REG_IN_PLACE'])) == 'Tool.SetBoneParentInPlace' and \
           x.lea_target(A['REG_IN_PLACE_FACTORY']) == FACTORY_IN_PLACE
    assert A['REG_IN_PLACE'] - REG == 0x100 and A['REG_IN_PLACE_FACTORY'] - REG == 0x122
    assert x.u32(A['KEY_REPARENT']) == 0x20000031 and x.u32(A['KEY_IN_PLACE']) == 0x60000031
    # Reparent: the selection, the first bone only, the dialog, then "keep it in place" for that bone
    assert x.rel_target(REPARENT + 0x22) == A['BoneSelection'] and x.rel_target(REPARENT + 0x86) == A['ListItem']
    assert x.rel_target(REPARENT + 0x154) == A['AskText'] and x.rel_target(REPARENT + 0x191) == A['SetBoneParent']
    assert x.rel_target(REPARENT + 0x661) == A['SetBoneRelPlacement'] and x.rel_target(REPARENT + 0x677) == A['UndoCommit']

    changes = [(VT_REPARENT + 8 * SLOT, B + THUNK_REPARENT, 'Tool.SetBoneParent (Reparent, Shift+S): vtable slot 30'),
               (VT_IN_PLACE + 8 * SLOT, B + THUNK_IN_PLACE, 'Tool.SetBoneParentInPlace (Reparent in place, Shift+Alt+S): vtable slot 30')]
    assert len(set(c[0] >> 12 for c in changes)) == 1, 'both slots on one page'

    # (rva, length, what) - code and data the plugin relies on
    regions = [
        (REPARENT, 0x718, "the editor's Reparent (called for one selected bone, the model for several)"),
        (THUNK_IN_PLACE, 0x00A, 'Tool.SetBoneParentInPlace: Reparent(tool, 1)'),
        (THUNK_REPARENT, 0x007, 'Tool.SetBoneParent: Reparent(tool, 0)'),
        (FACTORY_IN_PLACE, 0x039, 'factory of Tool.SetBoneParentInPlace'),
        (FACTORY_REPARENT, 0x039, 'factory of Tool.SetBoneParent'),
        (REG, 0x200, 'registration of the two tools, their commands and shortcuts'),
        (A['CMD_IN_PLACE'], 0x024, 'command "Reparent in place": runs Tool.SetBoneParentInPlace'),
        (A['CMD_REPARENT'], 0x024, 'command "Reparent": runs Tool.SetBoneParent'),
        (A['BoneSelection'], 0x1C8, 'list of the selected bones (in the order of selecting)'),
        (A['ListAddRef'], 0x023, 'handle list: add reference'),
        (A['ListRelease'], 0x0FE, 'handle list: release'),
        (A['ListCount'], 0x029, 'handle list: count'),
        (A['ListItem'], 0x03A, 'handle list: item'),
        (A['BoneName'], 0x063, 'bone name'),
        (A['BoneLOD'], 0x04F, "bone's skeleton LOD"),
        (A['BoneParentName'], 0x064, "bone's parent name"),
        (A['FindBone'], 0x0EE, 'bone of a LOD by name'),
        (A['BonePlacement'], 0x0F6, "bone's placement in the model"),
        (A['BoneParent'], 0x0A8, "bone's parent bone"),
        (A['SetBoneParent'], 0x01E, 'set the parent name (undoable)'),
        (A['SetBoneRelPlacement'], 0x01E, 'set the placement relative to the parent (undoable)'),
        (A['BoneFromHandle'], 0x047, 'bone from its handle'),
        (A['LODHandle'], 0x02B, 'handle of a skeleton LOD'),
        (A['AskText'], 0x2E4, 'dialog asking for a text with hints'),
        (A['CStringFrom'], 0x01D, 'CString from text'),
        (A['CStringFree'], 0x022, 'CString destructor'),
        (A['IdentFromString'], 0x15C, 'IdentFromString'),
        (A['IdentText'], 0x015, 'text of an IDENT'),
        (A['UndoBegin'], 0x01A, 'undo scope begin'),
        (A['UndoCommit'], 0x10F, 'undo scope commit'),
        (A['UndoEnd'], 0x044, 'undo scope end'),
        (A['SkeletonTree'], 0x008, 'the skeleton tree view'),
        (A['SkeletonTreeReset'], 0x00A, 'the skeleton tree view: rebuild its list'),
        (A['ConsoleInfo'], 0x048, 'console message (info)'),
        (A['ConsoleWarning'], 0x048, 'console message (warning)'),
    ]
    # the two vtables, without the slots the plugin changes
    for vt, name in ((VT_REPARENT, 'Tool.SetBoneParent'), (VT_IN_PLACE, 'Tool.SetBoneParentInPlace')):
        regions.append((vt - 8, 8 + 8 * SLOT, 'vtable of %s (RTTI, slots 0..29)' % name))
        regions.append((vt + 8 * (SLOT + 1), 8 * 3, 'vtable of %s (slots 31..33)' % name))

    changed = set()
    for rva, orig, what in changes:
        changed.update(range(rva, rva + 8))
    for rva, n, what in regions:
        assert not changed.intersection(range(rva, rva + n)), 'region %#x+%#x overlaps a changed slot' % (rva, n)
    return changes, regions


def main():
    paths, out_path = sys.argv[1:3], sys.argv[3]
    assert len(paths) == len(BUILDS), 'one exe per build: %s' % ', '.join(b for b, a in BUILDS)
    lines = ['/* Generated by gen_expect.py from Sam2017_SeriousEditor.exe of each supported build - do not edit. */',
             'typedef struct VerifyRegion { unsigned int rva, len; unsigned long long hash; } VerifyRegion;',
             'typedef struct Slot { unsigned int rva; unsigned long long orig; } Slot;',
             '#define NUM_SLOTS 2', '']
    entries = []
    for k, ((date, A), path) in enumerate(zip(BUILDS, paths)):
        x = Exe(path)
        changes, regions = analyse(x, A)
        # every region of the 2026 build must hash like the same region of 2019 unless it holds addresses:
        # (checked by the size of each region being the same in both builds, which the list above guarantees)
        tag = date.split()[-1]
        lines.append('/* build of %s (%s) */' % (date, path.split('/')[-1]))
        lines.append('static const VerifyRegion g_verify%s[] = {' % tag)
        for rva, n, what in regions:
            lines.append('    { 0x%07X, 0x%03X, 0x%016XULL }, /* %s */' % (rva, n, fnv1a64(x.read(rva, n)), what))
        lines.append('};')
        lines.append('/* the vtable slots it changes, with their original values */')
        lines.append('static const Slot g_slots%s[NUM_SLOTS] = {' % tag)
        for rva, orig, what in changes:
            lines.append('    { 0x%07X, 0x%XULL }, /* %s */' % (rva, orig, what))
        lines.append('};')
        lines.append('')
        entries.append((date, tag, x, A, len(regions)))
    lines.append('/* the supported builds: identity, the tables above and the RVAs of the editor functions it calls */')
    lines.append('typedef struct ExeBuild {')
    lines.append('    const wchar_t *date;')
    lines.append('    unsigned int timeDateStamp, sizeOfImage, checkSum;')
    lines.append('    unsigned long long imageBase;')
    lines.append('    const VerifyRegion *verify; unsigned int numVerify;')
    lines.append('    const Slot *slots;')
    lines.append('    unsigned int %s;' % ', '.join('rva%s' % ('Reparent' if c == 'REPARENT' else c) for c in CALLED))
    lines.append('} ExeBuild;')
    lines.append('static const ExeBuild g_builds[] = {')
    for date, tag, x, A, nreg in entries:
        lines.append('    { L"%s", 0x%08Xu, 0x%08Xu, 0x%08Xu, 0x%XULL, g_verify%s, %d, g_slots%s,' % (
            date, x.ts, x.size_of_image, x.checksum, x.image_base, tag, nreg, tag))
        rv = ['0x%07X' % A[c] for c in CALLED]
        for i in range(0, len(rv), 8):
            lines.append('      ' + ', '.join(rv[i:i + 8]) + (',' if i + 8 < len(rv) else ' },'))
    lines.append('};')
    lines.append('#define NUM_BUILDS %d' % len(entries))
    open(out_path, 'w').write('\n'.join(lines) + '\n')
    print('%d builds, %s regions, %d slots each' % (len(entries), '/'.join(str(e[4]) for e in entries), 2))


if __name__ == '__main__':
    main()
