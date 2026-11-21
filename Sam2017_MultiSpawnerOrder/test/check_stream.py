"""Decode the stream produced by the plugin's BuildStream() (native test run)
with the same parser that mirrors the engine's RegisterMetaData()."""
import sys, struct
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__file__), '..', 'tools'))
import meta
from pe import PE

def load(path):
    d = open(path, 'rb').read()
    nq = 288
    qs = list(struct.unpack_from('<%dQ' % nq, d, 0))
    o = nq * 8
    ns = struct.unpack_from('<I', d, o)[0]; o += 4
    strs = {}
    for _ in range(ns):
        a, l = struct.unpack_from('<QI', d, o); o += 12
        strs[a] = d[o:o + l].decode(); o += l
    genum = struct.unpack_from('<Q', d, o)[0]
    return qs, strs, genum

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
import os
BIN = os.environ.get('SAM2017_BIN', '.')
pe = PE(os.path.join(BIN, {'ed': 'Sam2017_SeriousEditor.exe', 'gm': 'Sam2017_Unrestricted.exe'}[which]))
qs, strs, genum = load(path)
fp = FakePE(pe, qs, strs)
ents = meta.parse_stream(fp, fp.base)
meta.dump(ents)
end = ents[-1]['end']
assert fp.u64(end) == 0xffffffffffffffff, 'terminator'
assert (end - fp.base) // 8 + 1 == len(qs), 'length'
en = [e for e in ents if e['tag'] == 1 and e['kind'] == 1]
assert len(en) == 1 and en[0]['name'] == 'ETemplateOrderMethod' and en[0]['global'] == genum and en[0]['size'] == 4
assert [v[0] for v in en[0]['values']] == [0, 1, 2, 3]
cls = [e for e in ents if e['tag'] == 1 and e['kind'] == 5 and e['name'] == 'CMultiSpawnerProperties'][0]
assert cls['h18'] == 6 and cls['size'] == 0x98
names = [m['name'] for m in cls['members']]
assert names == ['ep_pepSpawner', 'ep_hstpTemplateProvider', 'ep_bRandomizeTemplateOrder', 'ep_etomTemplateOrderMethod', 'ep_fRndBase'], names
m = cls['members'][3]
assert m['type'] == ('name', 'ETemplateOrderMethod') and m['offset'] == 0x94 and m['id'] == 6 and m['disp'] == 'Template order method' and m['str4'] == '' and m['idstr'] == '6'
ids = [mm['id'] for mm in cls['members']]
assert len(set(ids)) == len(ids)
offs = sorted((mm['offset'], mm['name']) for mm in cls['members'])
print('member offsets:', offs)
# the rest must be identical to the original decode
orig = meta.parse_stream(pe, {'ed': 0x142147d20, 'gm': 0x141596bb0}[which])
assert len(orig) + 1 == len(ents)
for a, b in zip(orig, ents[1:]):
    ka = {k: v for k, v in a.items() if k not in ('va', 'end', 'members', 'h18')}
    kb = {k: v for k, v in b.items() if k not in ('va', 'end', 'members', 'h18')}
    assert ka == kb, (a.get('name'), ka, kb)
print('STREAM OK for', which)
