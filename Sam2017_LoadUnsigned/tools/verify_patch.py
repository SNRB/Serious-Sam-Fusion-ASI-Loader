"""Apply the plugin's byte patches to a copy of each exe (in memory) and
disassemble the result, to confirm both gate sites now fall through to the
check's normal success path. Read-only on the real files."""
import sys, os, struct, subprocess, re
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pe import PE

T = {
 'ed': dict(name='Sam2017_SeriousEditor.exe', main=0x140bbcb0e, je=0x140bbcb08, succ=0x140bbcc97,
            sec=0x140d5caa5, proceed=0x140d5cadb),
 'gm': dict(name='Sam2017_Unrestricted.exe', main=0x140e35fee, je=0x140e35fe8, succ=0x140e36177,
            sec=0x1401f80f5, proceed=0x1401f812b),
 # build of 19 November 2026
 'ed26': dict(name='Sam2017_SeriousEditor.exe', main=0x140bbc94e, je=0x140bbc948, succ=0x140bbcad7,
              sec=0x140d944e5, proceed=0x140d9451b, bin='SAM2017_BIN_2026'),
 'gm26': dict(name='Sam2017_Unrestricted.exe', main=0x140d98a5e, je=0x140d98a58, succ=0x140d98be7,
              sec=0x1401f7a85, proceed=0x1401f7abb, bin='SAM2017_BIN_2026'),
}
MAIN_PATCH = bytes.fromhex('e984010000 90909090'.replace(' ',''))
SEC_PATCH  = bytes.fromhex('eb34')

import tempfile
def dis_one(raw, va):
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(raw); tmp = f.name
    try:
        out = subprocess.run(['objdump','-D','-b','binary','-m','i386:x86-64','-M','intel',
                              '--adjust-vma=%d'%va, tmp], capture_output=True).stdout.decode()
    finally:
        os.unlink(tmp)
    for l in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)', l)
        if m and int(m.group(1),16)==va: return m.group(3).strip()
    return '?'

def check(which, path):
    pe = PE(path); E = T[which]; ok = True
    def need(c,m):
        nonlocal ok
        if not c: ok=False; print('  FAIL',which,m)
    # patch a copy of a window around each site
    for site, patch, tgt, lbl in ((E['main'],MAIN_PATCH,E['succ'],'main'),(E['sec'],SEC_PATCH,E['proceed'],'sec')):
        raw = bytearray(pe.read(site, 16))
        raw[:len(patch)] = patch
        txt = dis_one(bytes(raw), site)
        need(txt.startswith('jmp') and ('0x%x'%tgt) in txt, '%s -> %r (want jmp 0x%x)'%(lbl,txt,tgt))
        print('  %s %-4s %#x: %s'%(which,lbl,site,txt))
    # the je above main still points at succ (unchanged)
    je = pe.read(E['je'],6); jt = E['je']+6+struct.unpack('<i',je[2:])[0]
    need(jt==E['succ'],'je target')
    print('%s: %s'%(which,'OK' if ok else 'FAILED'))
    return ok

if __name__=='__main__':
    allok=True
    for w in ('ed','gm','ed26','gm26'):
        p=os.path.join(os.environ.get(T[w].get('bin','SAM2017_BIN'),'.'),T[w]['name'])
        allok&=check(w,p)
    sys.exit(0 if allok else 1)
