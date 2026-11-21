"""Address table + verification of patch sites for both executables.
Prints C initializers for the plugin."""
import sys, struct, re
sys.path.insert(0, __import__('os').path.dirname(__file__))
import xref, meta

EXES = {
    'ed': dict(
        name='Sam2017_SeriousEditor.exe',
        RegisterMetaData=0x140b2e3e0,
        stream=0x142147d20,
        static_init=0x140f6dda0,
        class_global=0x143278808,
        msp_vtable=0x1421486d8,
        orig_GetTemplate=0x140f6d3f0,
        mse_vtable=0x142148838,
        spawner_vtable=0x1420c0518,
        ctor_inplace=0x140f6cd80,   # mov qword [rbx+0x8c],1   (11)
        ctor_new=0x140f6d2ba,       # mov qword [rbx+0x8c],1   (11)
        ctor_array=0x140f6ce9b,     # mov qword [rbx+0xc],1    (8)
        spawner_ctor_tail=0x140ecc766,  # add rsp,20; pop rbx; ret (6)
        HandleToPtr=0x140b2be80,
        PtrToHandle=0x140b2c220,
        Warn=0x140b1ffd0,
        WarnStr=0x1420c34e0,
        provider_GetTemplate=0x140ed5c90,
        spawner_ctor=0x140ecc5c0,
    ),
    'gm': dict(
        name='Sam2017_Unrestricted.exe',
        RegisterMetaData=0x140dc4d30,
        stream=0x141596bb0,
        static_init=0x1404f9320,
        class_global=0x141d99070,
        msp_vtable=0x141597598,
        orig_GetTemplate=0x1404f8960,
        mse_vtable=0x1415976f8,
        spawner_vtable=0x1415bf198,
        ctor_inplace=0x1404f8270,
        ctor_new=0x1404f882a,
        ctor_array=0x1404f83cb,
        spawner_ctor_tail=0x140522f86,
        HandleToPtr=0x140dfb9b0,
        PtrToHandle=0x140dfbd50,
        Warn=0x140dafa60,
        WarnStr=0x1417ac1f0,
        provider_GetTemplate=0x140812050,
        spawner_ctor=0x140522de0,
    ),
}

EXPECT = {
    'ctor_inplace': bytes.fromhex('48c7838c00000001000000'),
    'ctor_new': bytes.fromhex('48c7838c00000001000000'),
    'ctor_array': bytes.fromhex('48c7430c01000000'),
    'spawner_ctor_tail': bytes.fromhex('4883c4205bc3'),
}


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h


def branch_targets_into(x, start, end, lo, hi):
    """Find rel8/rel32 jumps/calls in [start,end) whose target lies in (lo,hi) exclusive of lo."""
    txt = x.dis(start, end)
    bad = []
    for l in txt.splitlines():
        m = re.match(r'\s+([0-9a-f]+):\s+(j\w+|call)\s+0x([0-9a-f]+)', l)
        if m:
            t = int(m.group(3), 16)
            if lo < t < hi:
                bad.append(l.strip())
    return bad


def check(which):
    A = EXES[which]
    x = xref.get(which)
    pe = x.pe
    out = {}
    print('=====', which, A['name'])
    print('TimeDateStamp %08x SizeOfImage %08x' % (pe.timestamp, pe.sizeofimage))
    out['ts'] = pe.timestamp
    out['soi'] = pe.sizeofimage
    # stream
    ents = meta.parse_stream(pe, A['stream'])
    end = ents[-1]['end']
    assert pe.u64(end) == 0xffffffffffffffff
    nq = (end - A['stream']) // 8 + 1  # include terminator
    raw = pe.read(A['stream'], nq * 8)
    out['stream_nq'] = nq
    out['stream_hash'] = fnv1a64(raw)
    cls = [e for e in ents if e['tag'] == 1 and e['kind'] == 5 and e['name'] == 'CMultiSpawnerProperties'][0]
    # qword indices
    q = lambda va: (va - A['stream']) // 8
    cls_va = cls['va']
    # layout: tag, kind, local, name, h18, size, h20, h30, base(2), disp, sname, nmem, members...
    i_h18 = q(cls_va) + 4
    i_nmem = q(cls_va) + 12
    assert pe.u64(A['stream'] + 8 * i_h18) == 5, 'h18'
    assert pe.u64(A['stream'] + 8 * i_nmem) == 4, 'nmem'
    m = cls['members']
    names = [mm['name'] for mm in m]
    assert names == ['ep_pepSpawner', 'ep_hstpTemplateProvider', 'ep_bRandomizeTemplateOrder', 'ep_fRndBase'], names
    i_ins = q(m[3]['va'])  # insert before ep_fRndBase record
    out['i_h18'] = i_h18
    out['i_nmem'] = i_nmem
    out['i_ins'] = i_ins
    print('stream qwords', nq, 'hash %016x' % out['stream_hash'], 'h18@', i_h18, 'nmem@', i_nmem, 'insert@', i_ins)
    # the record of ep_bRandomizeTemplateOrder for reference
    print('  BOOL member record:', [hex(pe.u64(m[2]['va'] + 8 * k)) for k in range(12)])
    # static init function
    si = pe.read(A['static_init'], 32)
    print('static init bytes', si.hex())
    assert si[:4] == bytes.fromhex('4883ec28') and si[4:7] == bytes.fromhex('488d0d')
    lea_t = A['static_init'] + 11 + struct.unpack('<i', si[7:11])[0]
    assert lea_t == A['stream'], hex(lea_t)
    assert si[11] == 0xe8
    call_t = A['static_init'] + 16 + struct.unpack('<i', si[12:16])[0]
    assert call_t == A['RegisterMetaData'], hex(call_t)
    assert si[16:21] == bytes.fromhex('4883c428c3')
    assert all(b == 0xcc for b in si[21:32]), si[21:32].hex()
    out['static_init_bytes'] = si
    # class global is in .data and zero-initialised in file
    gv = 0 if pe.va2off(A['class_global']) is None else pe.u64(A['class_global'])
    print('class global initial', hex(gv))
    assert gv == 0
    assert cls['global'] == A['class_global']
    # vtable slot
    slot = A['msp_vtable'] + 0x130
    assert pe.u64(slot) == A['orig_GetTemplate'], hex(pe.u64(slot))
    out['slot'] = slot
    # code refs to MSP vtable: exactly 4 (3 ctors + dtor)
    vr = x.code_refs(A['msp_vtable'])
    print('msp vtable refs', [hex(a) for a, e in vr])
    # ctor sites
    for k in ('ctor_inplace', 'ctor_new', 'ctor_array', 'spawner_ctor_tail'):
        b = pe.read(A[k], len(EXPECT[k]))
        assert b == EXPECT[k], (k, b.hex())
        fr = x.func_frags(A[k])
        lo, hi = A[k], A[k] + len(EXPECT[k])
        bad = []
        for s, e in fr:
            bad += branch_targets_into(x, s, e, lo, hi)
        print(k, hex(A[k]), 'func', hex(fr[0][0]), 'bad branch targets:', bad)
        assert not bad
    # GetTemplate only referenced from the vtable slot
    assert not x.code_refs(A['orig_GetTemplate'])
    assert x.data_refs(A['orig_GetTemplate']) == [slot]
    # provider GetTemplate called only from orig GetTemplate
    pr = x.code_refs(A['provider_GetTemplate'])
    assert len(pr) == 1 and A['orig_GetTemplate'] < pr[0][0] < A['orig_GetTemplate'] + 0x70
    # the provider uses vfunc 0x138 and the warn string / function
    ptxt = x.dis(A['provider_GetTemplate'], A['provider_GetTemplate'] + 0x150)
    assert ptxt.count('call   QWORD PTR [rax+0x138]') == 2
    assert ('# 0x%x' % A['WarnStr']) in ptxt and ('call   0x%x' % A['Warn']) in ptxt
    print('warn string:', pe.cstr(A['WarnStr']))
    # handle helpers referenced from orig GetTemplate / provider
    gtxt = x.dis(A['orig_GetTemplate'], A['orig_GetTemplate'] + 0x70)
    assert ('call   0x%x' % A['HandleToPtr']) in gtxt
    assert ('call   0x%x' % A['PtrToHandle']) in ptxt
    # spawner ctor writes the spawner vtable and the tail is the ctor's end
    stxt = x.dis(A['spawner_ctor'], A['spawner_ctor_tail'] + 6)
    assert ('# 0x%x' % A['spawner_vtable']) in stxt
    assert '+0x14c' not in stxt and '+0x148],ecx' in stxt
    # multi spawner entity vtable used by its constructors
    assert x.code_refs(A['mse_vtable'])
    # prologue bytes of helpers (for runtime sanity check)
    for k in ('RegisterMetaData', 'HandleToPtr', 'PtrToHandle', 'Warn', 'orig_GetTemplate'):
        out['pro_' + k] = pe.read(A[k], 16)
    out['A'] = A
    return out


if __name__ == '__main__':
    res = {w: check(w) for w in ('ed', 'gm')}
    for w in ('ed', 'gm'):
        assert res[w]['i_h18'] == res['ed']['i_h18'] and res[w]['i_nmem'] == res['ed']['i_nmem'] and res[w]['i_ins'] == res['ed']['i_ins'] and res[w]['stream_nq'] == res['ed']['stream_nq']
    import pickle
    pickle.dump(res, open('sites.pkl', 'wb'))
    print('ALL OK')
