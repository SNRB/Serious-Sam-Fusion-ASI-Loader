"""Auto-generate and verify the patch sites for Sam2017_LoadUnsigned.asi in the
executables of both supported builds. Writes ../src/targets.inc.
Usage:  SAM2017_BIN=<folder with both 7 June 2019 exes>
        SAM2017_BIN_2026=<folder with both 19 November 2026 exes>  python3 sig_sites.py
"""
import sys, os, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pe import PE

EXES = {
    # build of 7 June 2019
    'ed': dict(name='Sam2017_SeriousEditor.exe', bin='SAM2017_BIN', build='7 June 2019',
               main=0x140bbcb0e, main_je=0x140bbcb08, succ=0x140bbcc97, sec=0x140d5caa5),
    'gm': dict(name='Sam2017_Unrestricted.exe', bin='SAM2017_BIN', build='7 June 2019',
               main=0x140e35fee, main_je=0x140e35fe8, succ=0x140e36177, sec=0x1401f80f5),
    # build of 19 November 2026 (the same functions, moved)
    'ed26': dict(name='Sam2017_SeriousEditor.exe', bin='SAM2017_BIN_2026', build='19 November 2026',
                 main=0x140bbc94e, main_je=0x140bbc948, succ=0x140bbcad7, sec=0x140d944e5),
    'gm26': dict(name='Sam2017_Unrestricted.exe', bin='SAM2017_BIN_2026', build='19 November 2026',
                 main=0x140d98a5e, main_je=0x140d98a58, succ=0x140d98be7, sec=0x1401f7a85),
}

MAIN_EXPECT = bytes.fromhex('4585ff0f8580010000')          # test r15d,r15d ; jne <succ>
SEC_CTX     = bytes.fromhex('83c0fe83f802')                 # add eax,-2 ; cmp eax,2
SEC_JBE     = bytes.fromhex('7634')                         # jbe <proceed>


def build(which, path):
    E = EXES[which]
    pe = PE(path)
    ok = True
    def need(c, m):
        nonlocal ok
        if not c: ok = False; print('  FAIL', which, m)

    main = pe.read(E['main'], 9)
    need(main == MAIN_EXPECT, 'main bytes %s' % main.hex())
    # the je just above jumps to the success-continue label
    je = pe.read(E['main_je'], 6)
    need(je[:2] == b'\x0f\x84', 'je opcode')
    je_t = E['main_je'] + 6 + struct.unpack('<i', je[2:])[0]
    need(je_t == E['succ'], 'je target %x' % je_t)
    rel = E['succ'] - (E['main'] + 5)
    need(-0x80000000 <= rel <= 0x7fffffff, 'main rel')
    main_patch = b'\xe9' + struct.pack('<i', rel) + b'\x90\x90\x90\x90'

    sec_ctx = pe.read(E['sec'] - 6, 6)
    sec_jbe = pe.read(E['sec'], 2)
    need(sec_ctx == SEC_CTX, 'sec ctx %s' % sec_ctx.hex())
    need(sec_jbe == SEC_JBE, 'sec jbe %s' % sec_jbe.hex())
    sec_proceed = E['sec'] + 2 + sec_jbe[1]
    sec_patch = b'\xeb' + sec_jbe[1:2]                      # jbe rel8 -> jmp rel8 (same target)

    ts = pe.timestamp; soi = pe.sizeofimage
    print('%s: %s  ts=%#x soi=%#x  main=%#x sec=%#x (proceed %#x)' %
          (which, 'OK' if ok else 'FAILED', ts, soi, E['main'], E['sec'], sec_proceed))
    return ok, dict(E=E, ts=ts, soi=soi, main_patch=main_patch, sec_patch=sec_patch,
                    main_expect=main, sec_ctx8=sec_ctx + sec_jbe)


def cb(b): return '{' + ','.join('0x%02x' % x for x in b) + '}'

if __name__ == '__main__':
    out, allok = [], True
    for w in ('ed', 'gm', 'ed26', 'gm26'):
        p = os.path.join(os.environ.get(EXES[w]['bin'], '.'), EXES[w]['name'])
        ok, info = build(w, p); allok &= ok; out.append((w, info))
    if not allok: sys.exit(1)
    L = []
    for w, i in out:
        E = i['E']
        L += ['    { /* %s, build of %s */' % (E['name'], E['build']),
              '        L"%s", 0x%08XU, 0x%08XU,' % (E['name'], i['ts'], i['soi']),
              '        0x%XULL, %s, %s, /* main: test/jne -> jmp success-continue */' %
                  (E['main'], cb(i['main_expect']), cb(i['main_patch'])),
              '        0x%XULL, %s, %s, /* secondary: jbe (proceed) made unconditional */' %
                  (E['sec'], cb(i['sec_ctx8']), cb(i['sec_patch'])),
              '    },']
    dst = os.path.join(HERE, '..', 'src', 'targets.inc')
    open(dst, 'w').write('\n'.join(L) + '\n')
    print('wrote', os.path.normpath(dst))
