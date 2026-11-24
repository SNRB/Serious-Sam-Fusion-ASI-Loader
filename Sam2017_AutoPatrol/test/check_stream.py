"""Decode the stream produced by the plugin's BuildStream() (native test run)
with the same parser that mirrors the engine's RegisterMetaData(), and compare
it with the original stream of the executable."""
import sys, os, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import meta
from pe import PE

STREAM = {'ed': 0x1420e3150, 'gm': 0x1412f92a0, 'ed26': 0x142145bd0, 'gm26': 0x14128e8e0}
NQS = {'ed': 0x463e + 12, 'gm': 0x463e + 12, 'ed26': 0x469e + 12, 'gm26': 0x469e + 12}
EXE = {'ed': 'Sam2017_SeriousEditor.exe', 'gm': 'Sam2017_Unrestricted.exe',
       'ed26': 'Sam2017_SeriousEditor.exe', 'gm26': 'Sam2017_Unrestricted.exe'}
BIN = {'ed': 'SAM2017_BIN', 'gm': 'SAM2017_BIN', 'ed26': 'SAM2017_BIN_2026', 'gm26': 'SAM2017_BIN_2026'}


def load(path):
    d = open(path, 'rb').read()
    qs = list(struct.unpack_from('<%dQ' % NQ, d, 0))
    o = NQ * 8
    ns = struct.unpack_from('<I', d, o)[0]; o += 4
    strs = {}
    for _ in range(ns):
        a, l = struct.unpack_from('<QI', d, o); o += 12
        strs[a] = d[o:o + l].decode(); o += l
    return qs, strs


class FakePE:
    """Looks like PE for meta.parse_stream: stream qwords at a fake base, strings resolved."""
    def __init__(self, pe, qs, strs, base=0x7000000000):
        self.pe, self.qs, self.strs, self.base = pe, qs, strs, base
    def u64(self, va):
        if self.base <= va < self.base + 8 * len(self.qs):
            return self.qs[(va - self.base) // 8]
        return self.pe.u64(va)
    def cstr(self, va, maxlen=512):
        if va in self.strs:
            return self.strs[va]
        return self.pe.cstr(va, maxlen)


which, path = sys.argv[1], sys.argv[2]
NQ = NQS[which]
exe = os.path.join(os.environ.get(BIN[which], '.'), EXE[which])
pe = PE(exe)
qs, strs = load(path)
fp = FakePE(pe, qs, strs)
ents = meta.parse_stream(fp, fp.base)
end = ents[-1]['end']
assert fp.u64(end) == 0xffffffffffffffff, 'terminator'
assert (end - fp.base) // 8 + 1 == len(qs), 'length'
orig = meta.parse_stream(pe, STREAM[which])
assert len(orig) == len(ents), 'entry count'

cls = [e for e in ents if e['tag'] == 1 and e['kind'] == 5 and e['name'] == 'CPuppetProperties'][0]
ocls = [e for e in orig if e['tag'] == 1 and e['kind'] == 5 and e['name'] == 'CPuppetProperties'][0]
assert cls['h18'] == 152 and ocls['h18'] == 151, 'format'
assert cls['size'] == 0x188, 'size'
names = [m['name'] for m in cls['members']]
onames = [m['name'] for m in ocls['members']]
i = onames.index('ep_fActiveRangeOverride') + 1
assert i == len(onames), 'last member'
assert names == onames[:i] + ['ep_bAutoPatrol'] + onames[i:], names
m = cls['members'][i]
assert m['type'] == ('name', 'BOOL'), m['type']
assert (m['id'], m['disp'], m['idstr'], m['str4'], m['offset'], m['flags'], m['b40'], m['b60'], m['attrs']) == \
       (152, 'Auto patrol', '152', '', 0x184, 0, 0, 0, []), m
ids = [mm['id'] for mm in cls['members']]
assert len(set(ids)) == len(ids), 'unique member IDs'
offs = [mm['offset'] for mm in cls['members']]
assert offs == sorted(offs) and offs[-1] == 0x184, 'members in offset order'
for a, b in zip(ocls['members'], [mm for mm in cls['members'] if mm['name'] != 'ep_bAutoPatrol']):
    ka = {k: v for k, v in a.items() if k != 'va'}
    kb = {k: v for k, v in b.items() if k != 'va'}
    assert ka == kb, (ka, kb)
# every other entry must be identical to the original decode
for a, b in zip(orig, ents):
    if a is ocls:
        ka = {k: v for k, v in a.items() if k not in ('va', 'end', 'members', 'h18')}
        kb = {k: v for k, v in b.items() if k not in ('va', 'end', 'members', 'h18')}
    else:
        ka = {k: v for k, v in a.items() if k not in ('va', 'end')}
        kb = {k: v for k, v in b.items() if k not in ('va', 'end')}
        if 'members' in ka:
            ka['members'] = [{k: v for k, v in mm.items() if k != 'va'} for mm in ka['members']]
            kb['members'] = [{k: v for k, v in mm.items() if k != 'va'} for mm in kb['members']]
    assert ka == kb, (a.get('name'), ka, kb)
print('CPuppetProperties members around the new one:')
for mm in cls['members'][i - 3:i + 1]:
    print('    +0x%03x id=%-3d %-28r %s' % (mm['offset'], mm['id'], mm['disp'], mm['name']))
print('STREAM OK for', which, '(%d entries, %d qwords)' % (len(ents), len(qs)))
