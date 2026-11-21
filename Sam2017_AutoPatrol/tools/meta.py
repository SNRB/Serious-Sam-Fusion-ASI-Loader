"""Decoder for Serious Engine (Sam2017) static meta-data streams."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from pe import PE

KINDNAMES = {1: 'enum', 2: 'pointer', 3: 'reference', 4: 'staticarray', 5: 'class', 6: 'k6', 7: 'k7',
             8: 'k8', 9: 'function', 11: 'k11', 12: 'handle', 13: 'k13', 14: 'k14'}


class Reader:
    def __init__(self, pe, va):
        self.pe = pe
        self.va = va
        self.start = va

    def q(self):
        v = self.pe.u64(self.va)
        self.va += 8
        return v

    def d(self):
        return self.q() & 0xffffffff

    def i(self):
        v = self.q() & 0xffffffff
        return v - (1 << 32) if v & 0x80000000 else v

    def s(self):
        p = self.q()
        if p == 0:
            return None
        return self.pe.cstr(p)

    def typeref(self):
        mode = self.d()
        if mode == 1:
            return ('name', self.s())
        else:
            return ('idx', self.i())

    def header(self):
        name = self.s()
        a = self.d(); size = self.d(); c = self.d(); e = self.q()
        return dict(name=name, h18=a, size=size, h20=c, h30=e)

    def attrs(self):
        n = self.d()
        return [(self.s(), self.s()) for _ in range(n)]

    def functable(self):
        n = self.d()
        out = []
        for _ in range(n):
            bit = self.d()
            fn = self.q()
            out.append((bit, fn))
        return out


def parse_stream(pe, va, verbose=False):
    r = Reader(pe, va)
    entries = []
    while True:
        tag = r.i()
        if tag == -1:
            pass
        elif tag == 1:
            ent_va = r.va - 8
            kind = r.d()
            e = dict(tag=1, kind=kind, va=ent_va)
            if kind == 1:
                e['local'] = r.i()
                e.update(r.header())
                e['flagsenum'] = r.d()
                n = r.d()
                e['values'] = [(r.i(), r.s(), r.s()) for _ in range(n)]
                e['global'] = r.q()
            elif kind in (2, 3, 6, 7, 8, 11, 12, 13):
                e['local'] = r.i()
                e.update(r.header())
                e['ref'] = r.typeref()
            elif kind == 4:
                e['local'] = r.i()
                e.update(r.header())
                e['ref'] = r.typeref()
                n = r.d()
                e['dims'] = [r.d() for _ in range(n)]
            elif kind == 5:
                e['local'] = r.i()
                e.update(r.header())
                e['base'] = r.typeref()
                e['disp'] = r.s()
                e['sname'] = r.s()
                n = r.d()
                mems = []
                for _ in range(n):
                    m = dict(va=r.va)
                    m['type'] = r.typeref()
                    m['id'] = r.d()
                    m['disp'] = r.s()
                    m['name'] = r.s()
                    m['idstr'] = r.s()
                    m['str4'] = r.s()
                    m['offset'] = r.d()
                    m['flags'] = r.d()
                    m['b40'] = r.d()
                    m['b60'] = r.d()
                    m['attrs'] = r.attrs()
                    mems.append(m)
                e['members'] = mems
                nf = r.d()
                e['funcs'] = [r.typeref() for _ in range(nf)]
                e['attrs'] = r.attrs()
                e['ftable'] = r.functable()
                e['global'] = r.q()
            elif kind == 9:
                e['local'] = r.i()
                e.update(r.header())
                e['owner'] = r.typeref()
                e['ret'] = r.typeref()
                n = r.d()
                e['params'] = [(r.typeref(), r.s(), r.d()) for _ in range(n)]
                e['global'] = r.q()
            elif kind == 14:
                e['local'] = r.i()
                e.update(r.header())
                e['str'] = r.s()
                e['ref'] = r.typeref()
                e['ftable'] = r.functable()
                e['global'] = r.q()
            elif kind in (0, 10, 15, 16, 17):
                pass
            else:
                raise ValueError('unknown kind %d at %x' % (kind, ent_va))
            e['end'] = r.va
            entries.append(e)
        elif tag == 2:
            ent_va = r.va - 8
            e = dict(tag=2, va=ent_va)
            e['name'] = r.s()
            e['name2'] = r.s()
            e['addr'] = r.q()
            e['ref'] = r.typeref()
            e['d38'] = r.d()
            e['attrs'] = r.attrs()
            e['end'] = r.va
            entries.append(e)
        else:
            raise ValueError('bad tag %d at %x' % (tag, r.va - 8))
        if pe.u64(r.va) == 0xffffffffffffffff:
            break
    return entries


def fmt_ref(ref, entries):
    if ref is None:
        return '?'
    k, v = ref
    if k == 'name':
        return v
    for e in entries:
        if e.get('local') == v and e['tag'] == 1:
            return '#%d:%s' % (v, e.get('name'))
    return '#%d' % v


def dump(entries, out=sys.stdout):
    for e in entries:
        if e['tag'] == 2:
            print('GLOBAL %s / %s addr=%x type=%s d38=%d attrs=%s' % (e['name'], e['name2'], e['addr'], fmt_ref(e['ref'], entries), e['d38'], e['attrs']), file=out)
            continue
        k = e['kind']
        kn = KINDNAMES.get(k, str(k))
        hdr = 'h18=%d size=0x%x h20=0x%x h30=%x' % (e.get('h18', 0), e.get('size', 0), e.get('h20', 0), e.get('h30', 0))
        if k == 5:
            print('CLASS #%s %s (%s) base=%s disp=%r sname=%s %s global=%x @%x' % (e['local'], e['name'], kn, fmt_ref(e['base'], entries), e['disp'], e['sname'], hdr, e['global'], e['va']), file=out)
            for m in e['members']:
                print('    +0x%03x %-32s id=%-3d %-28s %-34s flags=0x%x b40=%d b60=%d attrs=%s' % (
                    m['offset'], fmt_ref(m['type'], entries), m['id'], repr(m['disp']), m['name'], m['flags'], m['b40'], m['b60'], m['attrs']), file=out)
            print('    funcs:', [fmt_ref(f, entries) for f in e['funcs']], 'attrs:', e['attrs'], file=out)
            print('    ftable:', ['%x:%x' % (b, f) for b, f in e['ftable']], file=out)
        elif k == 1:
            print('ENUM #%s %s %s flagsenum=%d global=%x @%x' % (e['local'], e['name'], hdr, e['flagsenum'], e['global'], e['va']), file=out)
            for v in e['values']:
                print('    %d %s %r' % v, file=out)
        elif k == 9:
            print('FUNC #%s %s owner=%s ret=%s params=%s global=%x' % (e['local'], e['name'], fmt_ref(e['owner'], entries), fmt_ref(e['ret'], entries),
                  [(fmt_ref(p[0], entries), p[1], p[2]) for p in e['params']], e['global']), file=out)
        elif k == 14:
            print('K14 #%s %s %s str=%r ref=%s ftable=%s global=%x' % (e['local'], e['name'], hdr, e['str'], fmt_ref(e['ref'], entries), ['%x:%x' % (b, f) for b, f in e['ftable']], e['global']), file=out)
        elif k == 4:
            print('%s #%s %s %s ref=%s dims=%s' % (kn.upper(), e['local'], e['name'], hdr, fmt_ref(e['ref'], entries), e['dims']), file=out)
        else:
            print('%s #%s %s %s ref=%s' % (kn.upper(), e.get('local'), e.get('name'), hdr, fmt_ref(e.get('ref'), entries)), file=out)


if __name__ == '__main__':
    import os
    BIN = os.environ.get('SAM2017_BIN', '.')
    path = {'ed': os.path.join(BIN, 'Sam2017_SeriousEditor.exe'), 'gm': os.path.join(BIN, 'Sam2017_Unrestricted.exe')}.get(sys.argv[1], sys.argv[1])
    pe = PE(path)
    ents = parse_stream(pe, int(sys.argv[2], 16))
    dump(ents)
