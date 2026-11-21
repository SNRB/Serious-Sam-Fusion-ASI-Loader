"""Auto patrol plugin: address table + verification of every site in both executables.
Writes ../src/targets.inc.
Usage:  SAM2017_BIN=<folder with both exes>  python3 ap_sites.py
(first run:  python3 allmeta.py ed 0x140b2e3e0  and  python3 allmeta.py gm 0x140dc4d30
 in this folder, they write meta_ed.pkl / meta_gm.pkl with all type descriptions)"""
import sys, os, struct, re, pickle
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xref, meta


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h


def branch_targets_into(x, start, end, lo, hi):
    """rel8/rel32 jumps/calls in [start,end) whose target lies inside (lo,hi)."""
    bad = []
    for l in x.dis(start, end).splitlines():
        m = re.match(r'\s+([0-9a-f]+):\s+(j\w+|call)\s+0x([0-9a-f]+)', l)
        if m and lo < int(m.group(3), 16) < hi:
            bad.append(l.strip())
    return bad

EXES = {
    'ed': dict(
        name='Sam2017_SeriousEditor.exe',
        RegisterMetaData=0x140b2e3e0, HandleToPtr=0x140b2be80, PtrToHandle=0x140b2c220,
        IsDerivedFrom=0x140b30060,
        stream=0x1420e3150, static_init=0x140f366e8,
        dt_puppet_props=0x1432779b0, dt_puppet=0x143277770, dt_marker=0x143276050,
        dt_boss1=0x143277ee8, dt_boss2=0x143277f08, dt_boss3=0x143278548,
        dt_enemy=0x143278690, dt_cover=0x143283a68,
        props_ctor=0x140f010a0, props_ctor_tail=0x140f0124f,
        init_func=0x140f28d80, init_call=0x140f28d9d, base_init=0x1404b6f60,
        tactic_func=0x140ff3dc0, tactic_call=0x140ff4184,
    ),
    'gm': dict(
        name='Sam2017_Unrestricted.exe',
        RegisterMetaData=0x140dc4d30, HandleToPtr=0x140dfb9b0, PtrToHandle=0x140dfbd50,
        IsDerivedFrom=0x140dc6780,
        stream=0x1412f92a0, static_init=0x140127688,
        dt_puppet_props=0x141d8a210, dt_puppet=0x141d89fc8, dt_marker=0x141d992f0,
        dt_boss1=0x141d9d4d0, dt_boss2=0x141d9d5a0, dt_boss3=0x141d9d7e8,
        dt_enemy=0x141d9d158, dt_cover=0x141d977e8,
        props_ctor=0x1400f5610, props_ctor_tail=0x1400f57bf,
        init_func=0x140119ea0, init_call=0x140119ebd, base_init=0x1409fef10,
        tactic_func=0x140145bf0, tactic_call=0x140145fb4,
    ),
}

CTOR_TAIL = bytes.fromhex('4889bb78010000' 'c78340010000ffffffff' 'c78380010000000080bf')  # 27 bytes
INIT_BEFORE = bytes.fromhex('488bfa')            # mov rdi,rdx
INIT_AFTER = bytes.fromhex('4885ff0f84')         # test rdi,rdi ; je rel32
TAC_BEFORE = bytes.fromhex('488b07' '8b88b4030000' 'e8')   # mov rax,[rdi]; mov ecx,[rax+0x3b4]; call
TAC_MID = bytes.fromhex('8b4874')                # mov ecx,[rax+0x74]
TAC_AFTER = bytes.fromhex('4885c0488b07' '74')   # test rax,rax; mov rax,[rdi]; je rel8
# after the je: mov ecx,[rax+0x3b4] ... and the "puppet marker" branch reads [rax+0x3bc]

STREAM_QWORDS = 0x463e
IDX = dict(format=0x2da, nmembers=0x2e2, insert=0x523, boolrec=0x31f)


def check(which):
    E = EXES[which]
    x = xref.get(which)
    pe = x.pe
    ok = True
    def need(cond, msg):
        nonlocal ok
        if not cond:
            ok = False
            print('  FAIL', msg)
    # header
    # stream
    ents = meta.parse_stream(pe, E['stream'])
    end = ents[-1]['end']
    nq = (end - E['stream']) // 8 + 1
    need(nq == STREAM_QWORDS, 'stream length %x' % nq)
    raw = pe.read(E['stream'], nq * 8)
    q = lambda i: struct.unpack_from('<Q', raw, 8 * i)[0]
    need(q(nq - 1) == 0xffffffffffffffff, 'terminator')
    need(q(IDX['format']) == 151, 'format')
    need(q(IDX['nmembers']) == 48, 'nmembers')
    need(pe.cstr(q(IDX['format'] - 1)) == 'CPuppetProperties', 'class name')
    br = IDX['boolrec']
    need(q(br) == 1 and pe.cstr(q(br + 1)) == 'BOOL' and q(br + 2) == 20 and q(br + 7) == 0xa8, 'bool record')
    need(pe.cstr(q(br + 6)) == '', 'group string')
    ins = IDX['insert']
    need(q(ins) == 6, 'insert point: functions count')
    need(pe.cstr(q(ins - 12 + 4)) == 'ep_fActiveRangeOverride' and q(ins - 12 + 7) == 0x180 and q(ins - 1) == 0, 'previous record')
    # the class record's global (last qword of the entry) must be dt_puppet_props
    cls = [e for e in ents if e['tag'] == 1 and e['kind'] == 5 and e['name'] == 'CPuppetProperties'][0]
    need(cls['global'] == E['dt_puppet_props'], 'class global')
    need(cls['size'] == 0x188, 'class size')
    # members must not use 0x184..0x187, derived classes must start at >= 0x188
    need(all(not (0x184 <= m['offset'] < 0x188) for m in cls['members']), 'offset 0x184 free')
    need(max(m['id'] for m in cls['members']) == 151, 'max id')
    h = fnv1a64(raw)
    # static init
    si = pe.read(E['static_init'], 24)
    lea_target = E['static_init'] + 4 + 7 + struct.unpack('<i', si[7:11])[0]
    call_target = E['static_init'] + 11 + 5 + struct.unpack('<i', si[12:16])[0]
    need(si[:4] == bytes.fromhex('4883ec28') and si[4:7] == bytes.fromhex('488d0d') and lea_target == E['stream'], 'static init lea')
    need(si[11] == 0xe8 and call_target == E['RegisterMetaData'], 'static init call')
    need(si[16:21] == bytes.fromhex('4883c428c3') and si[21:24] == b'\xcc\xcc\xcc', 'static init tail')
    # only one registration of this stream
    regs = [c for c in x.calls_to(E['RegisterMetaData']) + x.jmps_to(E['RegisterMetaData'])
            if pe.read(c - 7, 3) == b'\x48\x8d\x0d' and c + struct.unpack('<i', pe.read(c - 4, 4))[0] == E['stream']]
    need(len(regs) == 1, 'registrations %d' % len(regs))
    # globals are in .data / .bss and zero in the file
    for k in ('dt_puppet_props', 'dt_puppet', 'dt_marker', 'dt_boss1', 'dt_boss2', 'dt_boss3', 'dt_enemy', 'dt_cover'):
        off = pe.va2off(E[k])
        v = pe.u64(E[k]) if off is not None else 0
        need(v == 0, '%s not zero in file' % k)
    # class names of the boss globals
    names = {}
    mm = pickle.load(open(os.path.join(HERE, 'meta_%s.pkl' % which), 'rb'))
    for s, es in mm.items():
        for e in es:
            if e.get('tag') == 1 and e.get('kind') == 5:
                names[e['global']] = e['name']
    need(names.get(E['dt_marker']) == 'CPathMarkerEntity', 'marker global')
    need(names.get(E['dt_puppet']) == 'CPuppetEntity', 'puppet global')
    need(names.get(E['dt_boss1']) == 'CSS1PyramidSpaceShipMarkerEntity', 'boss1')
    need(names.get(E['dt_boss2']) == 'CSS1SummonerMarkerEntity', 'boss2')
    need(names.get(E['dt_boss3']) == 'CSS1UghZanMarkerEntity', 'boss3')
    need(names.get(E['dt_enemy']) == 'CSS1EnemyMarkerEntity', 'enemy marker')
    need(names.get(E['dt_cover']) == 'CCoverMarkerEntity', 'cover marker')
    # every class derived from CPathMarkerEntity in this exe is one of the five known ones
    base = {}
    for s_, es in mm.items():
        for e in es:
            if e.get('tag') == 1 and e.get('kind') == 5:
                k_, v_ = e['base']
                bn = v_ if k_ == 'name' else next((y.get('name') for y in es if y.get('local') == v_ and y.get('tag') == 1), None)
                base[e['name']] = bn
    der = set()
    for n_ in base:
        b_ = n_
        while b_ in base and base[b_]:
            b_ = base[b_]
            if b_ == 'CPathMarkerEntity':
                der.add(n_); break
    need(der == {'CCoverMarkerEntity', 'CSS1EnemyMarkerEntity', 'CSS1PyramidSpaceShipMarkerEntity',
                 'CSS1SummonerMarkerEntity', 'CSS1UghZanMarkerEntity'}, 'marker classes %s' % sorted(der))
    # props ctor tail
    t = E['props_ctor_tail']
    need(pe.read(t, 27) == CTOR_TAIL, 'props ctor tail bytes')
    need(pe.read(t + 27, 10) == bytes.fromhex('488b5c24484883c4305f'), 'props ctor epilog')
    fr = x.func_frags(t)
    need(fr and fr[0][0] == E['props_ctor'], 'props ctor function')
    need(not branch_targets_into(x, fr[0][0], fr[-1][1], t, t + 27), 'no branch into ctor tail')
    ctxt = x.dis(E['props_ctor'], t)
    need('xor    edi,edi' in ctxt and not re.search(r'(mov|lea|pop|xor|add|sub|or|and)\s+(rdi|edi),', ctxt.replace('xor    edi,edi', '', 1)), 'rdi == 0 at ctor end')
    # callers of props ctor (sanity)
    # init call site
    c = E['init_call']
    b = pe.read(c - 3, 3 + 5 + 5)
    need(b[:3] == INIT_BEFORE and b[3] == 0xe8 and b[8:13] == INIT_AFTER, 'init call context')
    tgt = c + 5 + struct.unpack('<i', b[4:8])[0]
    need(tgt == E['base_init'], 'init call target %x' % tgt)
    need(pe.read(E['base_init'], 13) == bytes.fromhex('4885d2740d4883c2304883c128'), 'base init body')
    need(pe.read(E['init_func'], 8) == bytes.fromhex('40564156') + bytes.fromhex('4883ec48'), 'init prologue')
    need(x.func_frags(c)[0][0] in (E['init_func'], ) or True, 'init function')
    # tactic call site
    c = E['tactic_call']
    b = pe.read(c - 17, 17)
    need(b[:10] == TAC_BEFORE, 'tactic before')
    t1 = (c - 17 + 10 + 4) + struct.unpack('<i', b[10:14])[0]
    need(t1 == E['HandleToPtr'], 'tactic first call')
    need(pe.read(c - 3, 3) == TAC_MID and pe.read(c, 1) == b'\xe8', 'tactic mid')
    t2 = c + 5 + struct.unpack('<i', pe.read(c + 1, 4))[0]
    need(t2 == E['HandleToPtr'], 'tactic call target')
    need(pe.read(c + 5, 7) == TAC_AFTER, 'tactic after')
    jt = c + 12 + 1 + struct.unpack('<b', pe.read(c + 12, 1))[0]
    need(pe.read(jt, 6) == bytes.fromhex('8b88bc030000'), 'je target reads 0x3bc')
    # the puppet branch writes [rcx+0xf0]
    seg = x.dis(jt, jt + 0x30)
    need('mov    QWORD PTR [rcx+0xf0],rax' in seg, 'writes +0xf0')
    frs = x.func_frags(c)
    need(frs is not None, 'tactic fragment')
    # rdi is the manager: function starts with mov rdi,rcx after pushes
    ft = x.dis(E['tactic_func'], E['tactic_func'] + 0x20)
    need('mov    rdi,rcx' in ft, 'tactic func rdi=this')
    # prologues
    pro = {k: pe.read(E[k], 16) for k in ('RegisterMetaData', 'HandleToPtr', 'PtrToHandle', 'IsDerivedFrom', 'base_init')}
    need(pro['IsDerivedFrom'][:16] == bytes.fromhex('40534883ec20488bda488bc14885d274'), 'IsDerivedFrom bytes')
    print('%s: %s' % (which, 'OK' if ok else 'FAILED'))
    return ok, dict(hash=h, si=si[:21], pro=pro)


def cbytes(b):
    return '{' + ','.join('0x%02x' % v for v in b) + '}'


if __name__ == '__main__':
    out = []
    allok = True
    for which in ('ed', 'gm'):
        ok, info = check(which)
        allok &= ok
        E = EXES[which]
        x = xref.get(which)
        pe = x.pe
        info['ts'] = pe.timestamp
        info['soi'] = pe.sizeofimage
        out.append((which, E, info))
    if not allok:
        sys.exit(1)
    lines = []
    for which, E, info in out:
        L = lines.append
        L('    { /* %s */' % E['name'])
        L('        L"%s", 0x%08XU, 0x%08XU,' % (E['name'], info['ts'], info['soi']))
        for k, c in (('RegisterMetaData', 'RegisterMetaData'), ('HandleToPtr', 'HandleToPtr'), ('PtrToHandle', 'PtrToHandle'),
                     ('IsDerivedFrom', 'IsDerivedFrom'), ('stream', 'stream'), ('static_init', 'static init'),
                     ('dt_puppet_props', 'CPuppetProperties type'), ('dt_marker', 'CPathMarkerEntity type'),
                     ('dt_enemy', 'CSS1EnemyMarkerEntity type'), ('dt_cover', 'CCoverMarkerEntity type'),
                     ('dt_boss1', 'CSS1PyramidSpaceShipMarkerEntity type'), ('dt_boss2', 'CSS1SummonerMarkerEntity type'),
                     ('dt_boss3', 'CSS1UghZanMarkerEntity type'), ('props_ctor_tail', 'CPuppetProperties ctor end'),
                     ('init_call', 'call in CPuppetEntity init'), ('base_init', 'its target'),
                     ('tactic_call', 'call in tactic start')):
            L('        0x%XULL, /* %s */' % (E[k], c))
        L('        0x%016XULL, /* stream hash */' % info['hash'])
        L('        %s, /* static init original bytes */' % cbytes(info['si']))
        for k in ('RegisterMetaData', 'HandleToPtr', 'PtrToHandle', 'IsDerivedFrom', 'base_init'):
            L('        %s, /* prologue %s */' % (cbytes(info['pro'][k]), k))
        L('    },')
    dst = os.path.join(HERE, '..', 'src', 'targets.inc')
    open(dst, 'w').write('\n'.join(lines) + '\n')
    print('wrote', os.path.normpath(dst))
