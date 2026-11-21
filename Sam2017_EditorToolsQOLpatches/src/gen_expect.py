#!/usr/bin/env python3
"""Generate exe_expect.h for Sam2017_EditorToolsQOLpatches.asi from the original Sam2017_SeriousEditor.exe
(7 June 2019 build): identity of the exe, FNV-1a 64 hashes of the code and data the plugin relies on, and the
vtable slots it changes, with their original values.
Usage: gen_expect.py <path-to-exe> <out.h>"""
import sys, struct

exe_path, out_path = sys.argv[1], sys.argv[2]
d = open(exe_path, 'rb').read()
e = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, e + 6)[0]
ts = struct.unpack_from('<I', d, e + 8)[0]
opt = e + 24
osz = struct.unpack_from('<H', d, e + 20)[0]
image_base = struct.unpack_from('<Q', d, opt + 24)[0]
size_of_image = struct.unpack_from('<I', d, opt + 56)[0]
checksum = struct.unpack_from('<I', d, opt + 64)[0]
secs = []
for i in range(nsec):
    o = opt + osz + 40 * i
    vsz, va, rsz, raw = struct.unpack_from('<IIII', d, o + 8)
    secs.append((va, vsz, raw, rsz))

def read(rva, n):
    for va, vsz, raw, rsz in secs:
        if va <= rva and rva + n <= va + rsz:
            return d[raw + rva - va: raw + rva - va + n]
    raise ValueError('rva %#x not in file data' % rva)

def q(rva):
    return struct.unpack('<Q', read(rva, 8))[0]

def u32(rva):
    return struct.unpack('<I', read(rva, 4))[0]

def cstr(va):
    rva = va - image_base
    out = b''
    while read(rva, 1) != b'\0':
        out += read(rva, 1)
        rva += 1
    return out.decode('latin1')

def rel_target(rva):
    """target of the call/jmp rel32 at rva (opcode e8/e9)"""
    assert read(rva, 1) in (b'\xe8', b'\xe9'), 'no call/jmp at %#x' % rva
    return rva + 5 + struct.unpack('<i', read(rva + 1, 4))[0]

def lea_target(rva):
    """target of the rip-relative lea at rva (48/4c 8d xx disp32)"""
    b = read(rva, 7)
    assert b[0] in (0x48, 0x4c) and b[1] == 0x8d, 'no lea at %#x' % rva
    return rva + 7 + struct.unpack('<i', b[3:7])[0]

def fnv1a64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h

B = image_base
# ---- the two Skeleton editor tools and what the plugin assumes about them
REPARENT = 0x19EE000                     # int (CTool *tool, int bInPlace): the editor's Reparent
THUNK_REPARENT, THUNK_IN_PLACE = 0x19EDFF0, 0x19EDFE0
VT_REPARENT, VT_IN_PLACE = 0x2621F70, 0x2622090           # vtables of Tool.SetBoneParent / ...InPlace
SLOT = 30                                # the slot the editor's tool activation runs (as for every CTool)
assert read(THUNK_REPARENT, 3) == b'\x33\xd2\xe9' and \
       rel_target(THUNK_REPARENT + 2) == REPARENT, 'Tool.SetBoneParent: xor edx,edx; jmp Reparent'
assert read(THUNK_IN_PLACE, 5) == b'\xba\x01\x00\x00\x00' and rel_target(THUNK_IN_PLACE + 5) == REPARENT, \
       'Tool.SetBoneParentInPlace: mov edx,1; jmp Reparent'
assert q(VT_REPARENT + 8 * SLOT) == B + THUNK_REPARENT and q(VT_IN_PLACE + 8 * SLOT) == B + THUNK_IN_PLACE
# the factories registered for the two tools install those vtables
FACTORY_REPARENT, FACTORY_IN_PLACE = 0x19EF2C0, 0x19EF280
assert lea_target(FACTORY_REPARENT + 0x20) == VT_REPARENT and lea_target(FACTORY_IN_PLACE + 0x20) == VT_IN_PLACE
# ...registered with these ids, and the shortcuts Shift+S and Shift+Alt+S (0x20000000 Shift, 0x40000000 Alt, 0x31 'S')
REG = 0x19EEB47
assert cstr(B + lea_target(REG)) == 'Tool.SetBoneParent' and lea_target(REG + 0x22) == FACTORY_REPARENT
assert cstr(B + lea_target(0x19EEC47)) == 'Tool.SetBoneParentInPlace' and lea_target(0x19EEC69) == FACTORY_IN_PLACE
assert u32(0x19EEC33) == 0x20000031 and u32(0x19EED33) == 0x60000031
# Reparent: the selection, the first bone only, the dialog, then "keep it in place" for that bone
assert rel_target(REPARENT + 0x22) == 0x19D9DB0 and rel_target(REPARENT + 0x86) == 0x15BFD30
assert rel_target(REPARENT + 0x154) == 0x1673DB0 and rel_target(REPARENT + 0x191) == 0x19D2780
assert rel_target(REPARENT + 0x661) == 0x19D27A0 and rel_target(REPARENT + 0x677) == 0x15C0680

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
    (0x19EF3F0, 0x024, 'command "Reparent in place": runs Tool.SetBoneParentInPlace'),
    (0x19EF420, 0x024, 'command "Reparent": runs Tool.SetBoneParent'),
    (0x19D9DB0, 0x1C8, 'list of the selected bones (in the order of selecting)'),
    (0x15BF7C0, 0x023, 'handle list: add reference'),
    (0x15BFE40, 0x0FE, 'handle list: release'),
    (0x15BFD00, 0x029, 'handle list: count'),
    (0x15BFD30, 0x03A, 'handle list: item'),
    (0x19D2330, 0x063, 'bone name'),
    (0x19D23A0, 0x04F, "bone's skeleton LOD"),
    (0x19D23F0, 0x064, "bone's parent name"),
    (0x19F5540, 0x0EE, 'bone of a LOD by name'),
    (0x19F5630, 0x0F6, "bone's placement in the model"),
    (0x19F5730, 0x0A8, "bone's parent bone"),
    (0x19D2780, 0x01E, 'set the parent name (undoable)'),
    (0x19D27A0, 0x01E, 'set the placement relative to the parent (undoable)'),
    (0x19DC1E0, 0x047, 'bone from its handle'),
    (0x19DC2E0, 0x02B, 'handle of a skeleton LOD'),
    (0x1673DB0, 0x2E4, 'dialog asking for a text with hints'),
    (0x0B25C50, 0x01D, 'CString from text'),
    (0x0B269A0, 0x022, 'CString destructor'),
    (0x0B2AB30, 0x15C, 'IdentFromString'),
    (0x0B2A9F0, 0x015, 'text of an IDENT'),
    (0x15C0510, 0x01A, 'undo scope begin'),
    (0x15C0680, 0x10F, 'undo scope commit'),
    (0x15C05B0, 0x044, 'undo scope end'),
    (0x19EC460, 0x008, 'the skeleton tree view'),
    (0x19EC2A0, 0x00A, 'the skeleton tree view: rebuild its list'),
    (0x0B1FFD0, 0x048, 'console message (info)'),
    (0x0B20970, 0x048, 'console message (warning)'),
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

lines = []
lines.append('/* Generated by gen_expect.py from %s - do not edit. */' % exe_path.split('/')[-1])
lines.append('#define EXE_TIMEDATESTAMP 0x%08Xu' % ts)
lines.append('#define EXE_SIZEOFIMAGE   0x%08Xu' % size_of_image)
lines.append('#define EXE_CHECKSUM      0x%08Xu' % checksum)
lines.append('#define EXE_IMAGEBASE     0x%XULL' % image_base)
lines.append('')
lines.append('static const struct VerifyRegion { unsigned int rva, len; unsigned long long hash; } g_verify[] = {')
for rva, n, what in regions:
    lines.append('    { 0x%07X, 0x%03X, 0x%016XULL }, /* %s */' % (rva, n, fnv1a64(read(rva, n)), what))
lines.append('};')
lines.append('')
lines.append('/* the vtable slots it changes, with their original values */')
lines.append('#define NUM_SLOTS %d' % len(changes))
lines.append('static const struct Slot { unsigned int rva; unsigned long long orig; } g_slots[] = {')
for rva, orig, what in changes:
    lines.append('    { 0x%07X, 0x%XULL }, /* %s */' % (rva, orig, what))
lines.append('};')
open(out_path, 'w').write('\n'.join(lines) + '\n')
print('%d regions, %d slots' % (len(regions), len(changes)))
