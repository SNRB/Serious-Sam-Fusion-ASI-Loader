#!/usr/bin/env python3
"""Generate exe_expect.h: for Sam2017_SeriousEditor.exe of each supported build (7 June 2019 and 19 November 2026)
its identity, the RVAs the plugin uses (editor functions, the console variables, the redirected calls), FNV-1a 64
hashes of the code the plugin relies on, and the original bytes of the call sites it redirects. The 2026 build is the
same code at other addresses; every assertion is checked for both.
Usage: gen_expect.py <exe of 7 June 2019> <exe of 19 November 2026> <out.h>"""
import sys, struct

BUILDS = [
    ('7 June 2019', dict(
        # call sites the plugin redirects
        CORE_DECLARATIONS=0x0B6A72B, HELP_CVAR_LIST=0x075B0D6, HELP_COMMAND=0x075B281, HELP_LOOKUP=0x075BFCD,
        EDITOR_COMMANDS=0x0160B48,
        # editor functions and data
        SECURITY_COOKIE=0x2A32688, RegisterDeclarations=0x0B2E3E0, DeclParser=0x0B2CC10, LoadCvarsHelp=0x075B430,
        CvarsHelpLoaded=0x2AE59D0, CvarDescArray=0x2903E28, CvarDescCtor=0x075ACF0, CvarDescArrayExpand=0x075AF20,
        CStringAssign=0x0B25F30, GetGlobalCmdHandler=0x1611F00, GetGlobalCmdHandlerBody=0x16740D0,
        RegisterCommand=0x1610600, RegisterShortcut=0x1610430, ConsoleInfo=0x0B1FFD0, CvarSave=0x0B572E0,
        HelpLookup=0x075BFB0, MemberNameGetter=0x0B730F0, IdToString=0x0B2A9F0,
        # the console variables (order of g_cvars) and an instruction of the editor that reads each
        cvars=[0x2AB9874, 0x1EFB2D8, 0x2A72FD8, 0x3279248, 0x3289A70, 0x3278EAC, 0x2AC5C50, 0x31DE4C8, 0x31DE960],
        readers=[[(0x0648693, 7)], [(0x0BD8BA6, 6), (0x1576FB2, 6)], [(0x054A570, 7)], [(0x0E8D721, 6)], [(0x139D2BF, 7)],
                 [(0x0F79886, 7)], [(0x0543A9F, 7)], [(0x0B5A5AE, 7)], [(0x0B6B5C7, 7)]])),
    ('19 November 2026', dict(
        CORE_DECLARATIONS=0x0B6A8DB, HELP_CVAR_LIST=0x075ADB6, HELP_COMMAND=0x075AF61, HELP_LOOKUP=0x075BCAD,
        EDITOR_COMMANDS=0x0160FE8,
        SECURITY_COOKIE=0x2A93348, RegisterDeclarations=0x0B2E0F0, DeclParser=0x0B2C920, LoadCvarsHelp=0x075B110,
        CvarsHelpLoaded=0x2B46E90, CvarDescArray=0x296CDB8, CvarDescCtor=0x075A9D0, CvarDescArrayExpand=0x075AC00,
        CStringAssign=0x0B25C30, GetGlobalCmdHandler=0x1651AC0, GetGlobalCmdHandlerBody=0x16B4350,
        RegisterCommand=0x16501C0, RegisterShortcut=0x164FFF0, ConsoleInfo=0x0B1FCE0, CvarSave=0x0B57300,
        HelpLookup=0x075BC90, MemberNameGetter=0x0B732E0, IdToString=0x0B2A700,
        cvars=[0x2B1ACA8, 0x1F3C158, 0x2AD4488, 0x32DA35C, 0x32EABA0, 0x32D9FBC, 0x2B27120, 0x323F988, 0x323FE20],
        readers=[[(0x0648733, 7)], [(0x0BD8A36, 6), (0x15B6CE2, 6)], [(0x054A380, 7)], [(0x0EC5E51, 6)], [(0x13D755F, 7)],
                 [(0x0FB2236, 7)], [(0x054389F, 7)], [(0x0B5A5BE, 7)], [(0x0B6B777, 7)]])),
]
CVAR_NAMES = ['hud_bShowGeometryInfo', 'res_bSaveInTextFormat', 'scr_bUnsecureScripting', 'prj_bUnlockAutoShotgunScope',
              'men_bMenuFrame', 'prj_bAllowLevelChangeInEditor', 'fxd_bShowDebug', 'fil_bReportFileOpen',
              'md_bReportReadStats']
SITES = ('CORE_DECLARATIONS', 'HELP_CVAR_LIST', 'HELP_COMMAND', 'HELP_LOOKUP', 'EDITOR_COMMANDS')
SITE_TARGET = dict(CORE_DECLARATIONS='RegisterDeclarations', HELP_CVAR_LIST='LoadCvarsHelp', HELP_COMMAND='LoadCvarsHelp',
                   HELP_LOOKUP='LoadCvarsHelp', EDITOR_COMMANDS='GetGlobalCmdHandler')
SITE_WHAT = dict(CORE_DECLARATIONS='call RegisterDeclarations in the Core module initializer',
                 HELP_CVAR_LIST='call LoadCvarsHelp in the list of all console variables',
                 HELP_COMMAND='call LoadCvarsHelp in the "help" console command',
                 HELP_LOOKUP='call LoadCvarsHelp in the help lookup used by the console tooltip',
                 EDITOR_COMMANDS='call GetGlobalCommandHandler before the editor registers its tool commands')
# RVAs in struct ExeBuild, in this order
FIELDS = ('SECURITY_COOKIE', 'RegisterDeclarations', 'LoadCvarsHelp', 'CvarsHelpLoaded', 'CvarDescArray', 'CvarDescCtor',
          'CvarDescArrayExpand', 'CStringAssign', 'GetGlobalCmdHandler', 'RegisterCommand', 'RegisterShortcut', 'ConsoleInfo',
          'MemberNameGetter', 'IdToString')


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
        self.load_config = struct.unpack_from('<I', d, opt + 112 + 8 * 10)[0]
        self.secs = []
        for i in range(nsec):
            o = opt + osz + 40 * i
            vsz, va, rsz, raw = struct.unpack_from('<IIII', d, o + 8)
            name = d[o:o + 8].rstrip(b'\0').decode()
            self.secs.append((va, vsz, raw, rsz, name))

    def read(self, rva, n):
        for va, vsz, raw, rsz, name in self.secs:
            if va <= rva and rva + n <= va + rsz:
                return self.d[raw + rva - va: raw + rva - va + n]
        raise ValueError('rva %#x not in file data' % rva)

    def section(self, rva):
        for va, vsz, raw, rsz, name in self.secs:
            if va <= rva < va + max(vsz, rsz):
                return name
        return None


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h


def analyse(x, A):
    B = x.image_base
    rel = lambda rva: rva + 5 + struct.unpack('<i', x.read(rva + 1, 4))[0]
    # the security cookie of the load configuration, still at the compiler's default value in the file
    assert struct.unpack('<Q', x.read(x.load_config + 0x58, 8))[0] == B + A['SECURITY_COOKIE'], 'security cookie'
    assert struct.unpack('<Q', x.read(A['SECURITY_COOKIE'], 8))[0] == 0x00002B992DDFA232, 'cookie default'
    # the redirected calls
    patches = []
    for name in SITES:
        rva = A[name]
        b = x.read(rva, 5)
        assert b[0] == 0xE8, 'site %#x is not a call' % rva
        assert rel(rva) == A[SITE_TARGET[name]], 'site %#x does not call %#x' % (rva, A[SITE_TARGET[name]])
        patches.append((rva, name, A[SITE_TARGET[name]], SITE_WHAT[name], b))
    # GetGlobalCommandHandler: jmp to  mov rax,[CommandHandler.Global] ; ret
    assert x.read(A['GetGlobalCmdHandler'], 1) == b'\xe9' and rel(A['GetGlobalCmdHandler']) == A['GetGlobalCmdHandlerBody']
    gb = x.read(A['GetGlobalCmdHandlerBody'], 8)
    assert gb[:3] == b'\x48\x8b\x05' and gb[7] == 0xc3
    # the member name getter: mov rcx,[rcx+18h] ; jmp ...
    mg = x.read(A['MemberNameGetter'], 9)
    assert mg[:3] == b'\x48\x8b\x49' and mg[3] in (0x18, 0x20) and mg[4] == 0xe9, mg.hex()
    assert A['MemberNameGetter'] + 9 + struct.unpack('<i', mg[5:9])[0] == A['IdToString'], 'member name getter -> IdToString'
    # every variable is read by the expected instruction (RIP-relative, it addresses that variable)
    for i, (cv, rds) in enumerate(zip(A['cvars'], A['readers'])):
        assert x.section(cv) in ('.data', '.rdata'), (CVAR_NAMES[i], x.section(cv))
        for rva, n in rds:
            ins = x.read(rva, n)
            imm = 1 if ins[0] == 0x83 else 0                      # cmp dword [rip+d],imm8
            disp_at = n - 4 - imm
            assert rva + n + struct.unpack('<i', ins[disp_at:disp_at + 4])[0] == cv, 'reader %#x of %s' % (rva, CVAR_NAMES[i])
    # (rva, length, what) - code/data the plugin depends on
    regions = [
        (A['RegisterDeclarations'], 0x12C, 'RegisterDeclarations(table): parses kind-2 records and registers them'),
        (A['DeclParser'], 0x123, 'variable declaration record parser (record layout the plugin produces)'),
        (A['LoadCvarsHelp'], 0x1DE, 'LoadCvarsHelp: clears the help records, reads Help/Cvars/ *_cvars.xml, sets the loaded flag'),
        (A['HelpLookup'], 0x01D, 'cvar help lookup, before its LoadCvarsHelp call'),
        (A['HelpLookup'] + 0x22, 0x085, 'cvar help lookup, after its LoadCvarsHelp call (first match, 0x58-byte records)'),
        (A['CvarDescCtor'], 0x071, 'CCvarDesc constructor'),
        (A['CvarDescArrayExpand'], 0x119, 'CCvarDesc array expand'),
        (A['CStringAssign'], 0x061, 'CString::operator=(const char *)'),
        (A['GetGlobalCmdHandler'], 0x005, 'GetGlobalCommandHandler: jmp'),
        (A['GetGlobalCmdHandlerBody'], 0x008, 'GetGlobalCommandHandler: mov rax,[CommandHandler.Global] ; ret'),
        (A['RegisterCommand'], 0x0B9, 'RegisterCommand(handler, id, tooltip, execute, update, extra)'),
        (A['RegisterShortcut'], 0x13C, 'RegisterShortcut(handler, command id, shortcut id, key, event, extra)'),
        (A['ConsoleInfo'], 0x048, 'console message of type CMT_INFO'),
        (A['CvarSave'], 0x15B, 'cvar saving: only cvars with the saved flag (0x4) are written to the INI'),
    ]
    assert A['HelpLookup'] + 0x1D == A['HELP_LOOKUP'] and A['HelpLookup'] + 0x22 == A['HELP_LOOKUP'] + 5
    for i, rds in enumerate(A['readers']):
        for rva, n in rds:
            regions.append((rva, n, 'reader of %s (0x%X)' % (CVAR_NAMES[i], B + A['cvars'][i])))
    # the 16 bytes before and after every redirected call must be the original too
    for rva, name, target, what, b in patches:
        regions.append((rva - 16, 16, 'code before the redirected call at 0x%X' % (B + rva)))
        regions.append((rva + 5, 16, 'code after the redirected call at 0x%X' % (B + rva)))
    # no verified byte may be one the plugin changes (or a second copy would not recognize the first)
    changed = set()
    for rva, name, target, what, b in patches:
        changed.update(range(rva + 1, rva + 5))
    changed.add(A['MemberNameGetter'] + 3)          # the byte the toggle command flips
    for rva, n, what in regions:
        assert not changed.intersection(range(rva, rva + n)), 'region %#x+%#x overlaps a changed byte' % (rva, n)
    return patches, regions


def main():
    paths, out_path = sys.argv[1:3], sys.argv[3]
    assert len(paths) == len(BUILDS)
    L = ['/* Generated by gen_expect.py from Sam2017_SeriousEditor.exe of each supported build - do not edit. */',
         'typedef struct VerifyRegion { unsigned int rva, len; unsigned long long hash; } VerifyRegion;',
         'typedef struct CallSite { unsigned int rva; unsigned char orig[5]; } CallSite;',
         '#define NUM_SITES %d' % len(SITES)]
    for i, name in enumerate(SITES):
        L.append('#define SITE_%-18s %d' % (name, i))
    L.append('#define NUM_CVARS_EXE %d   /* console variables per build, in the order of g_cvars */' % len(CVAR_NAMES))
    L.append('')
    entries = []
    for (date, A), path in zip(BUILDS, paths):
        x = Exe(path)
        patches, regions = analyse(x, A)
        tag = date.split()[-1]
        L.append('/* build of %s */' % date)
        L.append('static const VerifyRegion g_verify%s[] = {' % tag)
        for rva, n, what in regions:
            L.append('    { 0x%07X, 0x%03X, 0x%016XULL }, /* %s */' % (rva, n, fnv1a64(x.read(rva, n)), what))
        L.append('};')
        L.append('static const CallSite g_sites%s[NUM_SITES] = {' % tag)
        for rva, name, target, what, b in patches:
            L.append('    { 0x%07X, { %s } }, /* %s */' % (rva, ', '.join('0x%02X' % c for c in b), what))
        L.append('};')
        L.append('')
        entries.append((date, tag, x, A, len(regions)))
    L.append('/* the supported builds: identity, the tables above, the RVAs it uses */')
    L.append('typedef struct ExeBuild {')
    L.append('    const wchar_t *date;')
    L.append('    unsigned int timeDateStamp, sizeOfImage, checkSum;')
    L.append('    unsigned long long imageBase;')
    L.append('    const VerifyRegion *verify; unsigned int numVerify;')
    L.append('    const CallSite *sites;')
    L.append('    unsigned int %s;' % ', '.join('rva' + f for f in FIELDS))
    L.append('    unsigned int rvaCvar[NUM_CVARS_EXE];')
    L.append('} ExeBuild;')
    L.append('static const ExeBuild g_builds[] = {')
    for date, tag, x, A, nreg in entries:
        L.append('    { L"%s", 0x%08Xu, 0x%08Xu, 0x%08Xu, 0x%XULL, g_verify%s, %d, g_sites%s,' % (
            date, x.ts, x.size_of_image, x.checksum, x.image_base, tag, nreg, tag))
        L.append('      ' + ', '.join('0x%07X' % A[f] for f in FIELDS) + ',')
        L.append('      { ' + ', '.join('0x%07X' % c for c in A['cvars']) + ' } },')
    L.append('};')
    L.append('#define NUM_BUILDS %d' % len(entries))
    open(out_path, 'w').write('\n'.join(L) + '\n')
    print('%d builds, %s regions' % (len(entries), '/'.join(str(e[4]) for e in entries)))


if __name__ == '__main__':
    main()
