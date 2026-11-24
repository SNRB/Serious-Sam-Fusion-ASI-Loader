/*
 * Sam2017_AutoPatrol.asi
 * ---------------------------------------------------------------------------
 * Adds an "Auto patrol" check box to every character entity (all properties
 * derived from CPuppetProperties: Legged character, Aircraft, Turret,
 * Hovercraft, Rolling ball, Spaceship, ...) of Serious Sam Fusion 2017,
 * builds of 7 June 2019 and 19 November 2026:
 *     Sam2017_SeriousEditor.exe  and  Sam2017_Unrestricted.exe
 *
 * Off by default. When checked, and the character has a Tactic (its own, or
 * the "Tactics holder" of the spawner that spawned it), then every time it
 * starts that tactic - normally right after it is spawned or the level
 * starts - the nearest path marker (Path marker, Enemy marker (SS1), Cover
 * marker, MultiPath marker, ...) becomes its Tactic marker, replacing any
 * marker set on the character, on the spawner or on the tactic. The tactic
 * then works exactly as before (ordered moving, random marker, ...), only the
 * first target marker is the nearest one.
 *
 * How it works (all in memory, the .exe files are not modified):
 *   1. The type description (meta data stream) that contains CPuppetProperties
 *      is copied and registered instead of the original, with a new member
 *      BOOL ep_bAutoPatrol ("Auto patrol", member ID 152) at offset 0x184
 *      (4 bytes of padding at the end of the 0x188-byte object, also padding
 *      in all 34 derived classes), added after the last member ("Active range
 *      override"), so the members stay in offset order like in every engine
 *      class. The type's format number goes from 151 to 152, so levels saved
 *      before still load (the engine converts older formats).
 *   2. The CPuppetProperties constructor (used by all derived classes) also
 *      clears offset 0x184 (new characters and old levels start unchecked).
 *   3. CPuppetEntity::InitializeFromProperties: the call of its CEntity part
 *      is redirected; afterwards the plugin notes whether the character has
 *      "Auto patrol" checked (spawned characters lose their properties right
 *      after being created, so the setting is kept per entity).
 *   4. Where CCharacterMoverManager starts a tactic, the call that resolves
 *      the tactic's own marker is redirected: for an Auto patrol character
 *      starting its own (default) tactic, the nearest marker is written to
 *      the character's en_henTacticMarker and "no marker on the tactic" is
 *      returned, so the engine continues with that marker.
 *
 * Needs an ASI loader that loads plugins when the program starts, before the
 * engine registers its types (the msimg32.dll loader of the Sam2017 mods).
 * ---------------------------------------------------------------------------
 */

#include "ap_common.h"

/* ------------------------------------------------------------------------- */
/* kernel32                                                                  */
/* ------------------------------------------------------------------------- */
#define IMPORT __declspec(dllimport)
IMPORT void *__stdcall GetModuleHandleW(const wchar_t *name);
IMPORT u32   __stdcall GetModuleFileNameW(void *mod, wchar_t *buf, u32 n);
IMPORT void *__stdcall VirtualAlloc(void *addr, u64 size, u32 type, u32 prot);
IMPORT int   __stdcall VirtualProtect(void *addr, u64 size, u32 prot, u32 *old);
IMPORT int   __stdcall FlushInstructionCache(void *proc, const void *addr, u64 size);
IMPORT void *__stdcall GetCurrentProcess(void);
IMPORT void *__stdcall CreateFileW(const wchar_t *name, u32 access, u32 share, void *sa,
                                   u32 disp, u32 flags, void *tmpl);
IMPORT int   __stdcall WriteFile(void *h, const void *buf, u32 n, u32 *written, void *ov);
IMPORT int   __stdcall CloseHandle(void *h);
IMPORT void *__stdcall LoadLibraryW(const wchar_t *name);
IMPORT void *__stdcall GetProcAddress(void *mod, const char *name);
IMPORT u32   __stdcall GetCurrentProcessId(void);
IMPORT int   __stdcall DisableThreadLibraryCalls(void *mod);
IMPORT u32   __stdcall GetLastError(void);
IMPORT u32   __stdcall GetEnvironmentVariableW(const wchar_t *name, wchar_t *buf, u32 n);
IMPORT int   __stdcall SetEnvironmentVariableW(const wchar_t *name, const wchar_t *value);
IMPORT void  __stdcall OutputDebugStringW(const wchar_t *s);

#define MEM_COMMIT              0x1000
#define MEM_RESERVE             0x2000
#define PAGE_EXECUTE_READ       0x20
#define PAGE_EXECUTE_READWRITE  0x40
#define GENERIC_WRITE           0x40000000
#define FILE_SHARE_READ         1
#define CREATE_ALWAYS           2
#define FILE_ATTRIBUTE_NORMAL   0x80
#define INVALID_HANDLE          ((void *)(i64)-1)
#define DLL_PROCESS_ATTACH      1

/* no CRT: the compiler may still emit calls to these */
void *memset(void *d, int c, u64 n)
{
    u8 *p = (u8 *)d;
    while (n--) *p++ = (u8)c;
    return d;
}
void *memcpy(void *d, const void *s, u64 n)
{
    u8 *p = (u8 *)d;
    const u8 *q = (const u8 *)s;
    while (n--) *p++ = *q++;
    return d;
}
int memcmp(const void *a, const void *b, u64 n)
{
    const u8 *p = (const u8 *)a, *q = (const u8 *)b;
    for (; n; n--, p++, q++)
        if (*p != *q) return *p < *q ? -1 : 1;
    return 0;
}
int _fltused = 0;

static const Target g_targets[] = {
#include "targets.inc"
};

/* ------------------------------------------------------------------------- */
/* small helpers                                                             */
/* ------------------------------------------------------------------------- */
static u32 wlen(const wchar_t *s) { u32 n = 0; while (s[n]) n++; return n; }

static void wcat(wchar_t *d, u32 cap, const wchar_t *s)
{
    u32 n = wlen(d);
    while (*s && n + 1 < cap) d[n++] = *s++;
    d[n] = 0;
}

static void wcathex(wchar_t *d, u32 cap, u64 v)
{
    wchar_t b[20];
    int i = 0;
    b[i++] = L'0'; b[i++] = L'x';
    int started = 0;
    for (int sh = 60; sh >= 0; sh -= 4) {
        u32 nib = (u32)(v >> sh) & 15;
        if (nib || started || sh == 0) {
            b[i++] = (wchar_t)(nib < 10 ? L'0' + nib : L'A' + nib - 10);
            started = 1;
        }
    }
    b[i] = 0;
    wcat(d, cap, b);
}

static void wcatdec(wchar_t *d, u32 cap, u64 v)
{
    wchar_t b[24];
    int i = 23;
    b[i] = 0;
    do { b[--i] = (wchar_t)(L'0' + v % 10); v /= 10; } while (v);
    wcat(d, cap, b + i);
}

static int wieq(const wchar_t *a, const wchar_t *b)
{
    for (;; a++, b++) {
        wchar_t x = *a, y = *b;
        if (x >= L'A' && x <= L'Z') x += 32;
        if (y >= L'A' && y <= L'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* ------------------------------------------------------------------------- */
/* log file next to the plugin                                               */
/* ------------------------------------------------------------------------- */
static wchar_t g_plugPath[520];     /* full path of this .asi */
static wchar_t g_plugName[128];     /* file name only         */
static wchar_t g_log[8192];

static void Log(const wchar_t *s) { wcat(g_log, 8192, s); }
static void LogHex(const wchar_t *s, u64 v) { Log(s); wcathex(g_log, 8192, v); }

static void LogFlush(void)
{
    wchar_t path[540];
    path[0] = 0;
    wcat(path, 540, g_plugPath);
    u32 n = wlen(path);
    while (n && path[n - 1] != L'.' && path[n - 1] != L'\\' && path[n - 1] != L'/') n--;
    if (n && path[n - 1] == L'.') path[n - 1] = 0;   /* strip extension */
    wcat(path, 540, L".log");
    void *h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE) return;
    /* write as UTF-8 (all text is ASCII here) */
    static char buf[8192];
    u32 len = 0;
    for (u32 i = 0; g_log[i] && len < sizeof(buf); i++) {
        wchar_t c = g_log[i];
        if (c == L'\n') { if (len + 2 > sizeof(buf)) break; buf[len++] = '\r'; buf[len++] = '\n'; }
        else buf[len++] = (char)(c < 128 ? c : '?');
    }
    u32 wr;
    WriteFile(h, buf, len, &wr, 0);
    CloseHandle(h);
    OutputDebugStringW(g_log);
}

static void Message(const wchar_t *text)
{
    /* one message box per process, even if several copies are installed
       (the variable name holds the process ID: child processes inherit the
       environment, but must still be able to warn on their own) */
    wchar_t var[64], tmp[4];
    var[0] = 0;
    wcat(var, 64, L"SAM2017_AP_WARNED_");
    wcatdec(var, 64, GetCurrentProcessId());
    if (GetEnvironmentVariableW(var, tmp, 4)) return;
    SetEnvironmentVariableW(var, L"1");
    void *u32dll = LoadLibraryW(L"user32.dll");
    if (!u32dll) return;
    typedef int (__stdcall *MessageBoxW_t)(void *, const wchar_t *, const wchar_t *, u32);
    MessageBoxW_t mb = (MessageBoxW_t)GetProcAddress(u32dll, "MessageBoxW");
    if (mb) mb(0, text, L"Sam2017 mods - Auto patrol", 0x30 /* MB_ICONWARNING */);
}

#include "ap_logic.h"

/* ------------------------------------------------------------------------- */
/* patching                                                                  */
/* ------------------------------------------------------------------------- */
static u64 Fnv1a64(const u8 *p, u64 n)
{
    u64 h = 0xcbf29ce484222325ULL;
    while (n--) { h ^= *p++; h *= 0x100000001b3ULL; }
    return h;
}

static int Patch(u64 addr, const void *bytes, u32 n)
{
    u32 old;
    if (!VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old)) {
        LogHex(L"VirtualProtect failed at ", addr);
        LogHex(L", error ", GetLastError());
        Log(L"\n");
        return 0;
    }
    memcpy((void *)addr, bytes, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
    return 1;
}

static int Rel32(u64 from_next, u64 to, i32 *rel)
{
    i64 d = (i64)(to - from_next);
    if (d > 0x7fffffffLL || d < -0x80000000LL) return 0;
    *rel = (i32)d;
    return 1;
}

/* target of the rel32 call/jmp at 'site' */
static u64 CallTarget(u64 site) { return site + 5 + (i64)*(const i32 *)(site + 1); }

static u8 *AllocNear(u64 base, u64 size)
{
    /* within +-2 GB of all patched code. The other Sam2017 mod plugins prefer
       the address right after the image, so start 128 MB further. */
    u64 start = (base + size + 0xFFFF) & ~0xFFFFULL;
    for (u64 a = start + 0x08000000ULL; a < start + 0x40000000ULL; a += 0x10000) {
        void *p = VirtualAlloc((void *)a, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return (u8 *)p;
    }
    for (u64 a = start; a < start + 0x08000000ULL; a += 0x10000) {
        void *p = VirtualAlloc((void *)a, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return (u8 *)p;
    }
    for (u64 a = (base - 0x10000) & ~0xFFFFULL; a > base - 0x40000000ULL; a -= 0x10000) {
        void *p = VirtualAlloc((void *)a, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return (u8 *)p;
    }
    return 0;
}

static void FailMsg(const wchar_t *why, u64 addr)
{
    static wchar_t m[1024];
    m[0] = 0;
    wcat(m, 1024, g_plugName);
    wcat(m, 1024, L" was not applied to this program: ");
    wcat(m, 1024, why);
    if (addr) { wcat(m, 1024, L" (at "); wcathex(m, 1024, addr); wcat(m, 1024, L")"); }
    wcat(m, 1024, L".\n\nThe program continues without this mod.");
    Log(L"NOT APPLIED: ");
    Log(why);
    if (addr) LogHex(L" at ", addr);
    Log(L"\n");
    Message(m);
}

/* original code */
static const u8 kCtorTail[27] = {
    0x48, 0x89, 0xBB, 0x78, 0x01, 0x00, 0x00,                       /* mov [rbx+0x178],rdi            */
    0xC7, 0x83, 0x40, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,     /* mov dword [rbx+0x140],-1       */
    0xC7, 0x83, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0xBF };   /* mov dword [rbx+0x180],-1.0f    */
/* the same, plus  [rbx+0x184] = 0  (rdi is 0 here, rcx is free) */
static const u8 kNewCtorTail[27] = {
    0x48, 0x89, 0xBB, 0x78, 0x01, 0x00, 0x00,                       /* mov [rbx+0x178],rdi            */
    0x83, 0x8B, 0x40, 0x01, 0x00, 0x00, 0xFF,                       /* or  dword [rbx+0x140],-1       */
    0xB9, 0x00, 0x00, 0x80, 0xBF,                                   /* mov ecx,0xBF800000             */
    0x48, 0x89, 0x8B, 0x80, 0x01, 0x00, 0x00,                       /* mov [rbx+0x180],rcx  (+0x184=0) */
    0x90 };
static const u8 kInitBefore[3] = {0x48, 0x8B, 0xFA};                      /* mov rdi,rdx              */
static const u8 kInitAfter[5]  = {0x48, 0x85, 0xFF, 0x0F, 0x84};          /* test rdi,rdi ; je        */
static const u8 kTacBefore[10] = {0x48, 0x8B, 0x07, 0x8B, 0x88, 0xB4, 0x03, 0x00, 0x00, 0xE8}; /* mov rax,[rdi]; mov ecx,[rax+0x3b4]; call */
static const u8 kTacMid[3]     = {0x8B, 0x48, 0x74};                      /* mov ecx,[rax+0x74]       */
static const u8 kTacAfter[7]   = {0x48, 0x85, 0xC0, 0x48, 0x8B, 0x07, 0x74}; /* test rax,rax; mov rax,[rdi]; je */

static void Apply(void)
{
    u8 *exe = (u8 *)GetModuleHandleW(0);
    u8 *nt = exe + *(u32 *)(exe + 0x3c);
    u32 ts = *(u32 *)(nt + 8);
    u32 soi = *(u32 *)(nt + 24 + 56);

    wchar_t exePath[520];
    exePath[0] = 0;
    GetModuleFileNameW(0, exePath, 520);
    const wchar_t *exeName = exePath;
    for (const wchar_t *p = exePath; *p; p++)
        if (*p == L'\\' || *p == L'/') exeName = p + 1;

    Log(L"Sam2017 mods - Auto patrol for characters\n");
    Log(L"program: "); Log(exePath);
    LogHex(L"\nTimeDateStamp ", ts); LogHex(L", SizeOfImage ", soi);
    LogHex(L", base ", (u64)exe); Log(L"\n");

    const Target *T = 0;
    for (u32 i = 0; i < sizeof(g_targets) / sizeof(g_targets[0]); i++)
        if (g_targets[i].timeDateStamp == ts && g_targets[i].sizeOfImage == soi) T = &g_targets[i];
    if (!T) {
        for (u32 i = 0; i < sizeof(g_targets) / sizeof(g_targets[0]); i++) {
            if (wieq(exeName, g_targets[i].exeName)) {
                static wchar_t m[1024];
                m[0] = 0;
                wcat(m, 1024, exeName);
                wcat(m, 1024, L" is not one of the original executables (builds of 7 June 2019 and "
                              L"19 November 2026) that ");
                wcat(m, 1024, g_plugName);
                wcat(m, 1024, L" was made for. If it is a patched exe, restore the original one "
                              L"(Steam: Properties > Installed Files > Verify integrity of game files).\n\n"
                              L"The program continues without this mod.");
                Log(L"NOT APPLIED: unknown build of a supported program\n");
                Message(m);
                return;
            }
        }
        Log(L"nothing to do: not a program this plugin knows\n");
        return;
    }
    if ((u64)exe != 0x140000000ULL) { FailMsg(L"unexpected image base", (u64)exe); return; }

    /* --- checks -------------------------------------------------------- */
    const u8 *si = (const u8 *)T->staticInit;
    if (memcmp(si, T->staticInitBytes, 21) != 0) {
        /* our own patch (mov rcx,imm64 / mov rax,imm64 / jmp rax): another copy of this plugin */
        if (si[0] == 0x48 && si[1] == 0xB9 && si[10] == 0x48 && si[11] == 0xB8 && si[20] == 0xFF &&
            si[21] == 0xE0 && *(const u64 *)(si + 12) == T->RegisterMetaData) {
            Log(L"already applied by another copy of this plugin, skipped\n");
            return;
        }
        FailMsg(L"the program code it relies on is different from what it expects. "
                L"Another mod may have changed it", T->staticInit);
        return;
    }
    for (u32 i = 21; i < 24; i++)
        if (si[i] != 0xCC) { FailMsg(L"unexpected program code", T->staticInit + i); return; }

    if (*(u64 *)T->dtPuppetProps != 0) {
        static wchar_t m[1024];
        m[0] = 0;
        wcat(m, 1024, g_plugName);
        wcat(m, 1024, L" was loaded after the program had started, too late for its changes. "
                      L"Install the msimg32.dll loader that comes with the Sam2017 mods (copy msimg32.dll "
                      L"next to the exe, with this plugin in the plugins folder), or use an ASI loader that "
                      L"loads plugins before the program starts.\n\nThe program continues without this mod.");
        Log(L"NOT APPLIED: loaded too late (character types already registered)\n");
        Message(m);
        return;
    }

    if (T->streamQwords > STREAM_QWORDS_MAX || T->idxInsert >= T->streamQwords ||
        Fnv1a64((const u8 *)T->stream, (u64)T->streamQwords * 8) != T->streamHash) {
        FailMsg(L"the character meta data is different from what it expects", T->stream);
        return;
    }
    const u64 *src = (const u64 *)T->stream;
    if (src[T->idxFormat] != OLD_FORMAT || src[T->idxNMembers] != OLD_NMEMBERS ||
        src[T->idxBoolRec] != 1 || src[T->idxBoolRec + 2] != 20 || src[T->idxBoolRec + 7] != 0xa8 ||
        src[T->idxInsert - 12 + 7] != 0x180 || src[T->idxInsert] != 6 ||
        src[T->streamQwords - 1] != ~0ULL) {
        FailMsg(L"unexpected meta data layout", T->stream);
        return;
    }

    const wchar_t *changed = L"the program code it changes was already changed by something else";
    if (memcmp((void *)T->propsCtorTail, kCtorTail, 27)) { FailMsg(changed, T->propsCtorTail); return; }
    {
        const u8 *c = (const u8 *)T->initCall;
        if (memcmp(c - 3, kInitBefore, 3) || c[0] != 0xE8 || CallTarget(T->initCall) != T->baseInit ||
            memcmp(c + 5, kInitAfter, 5)) {
            FailMsg(changed, T->initCall);
            return;
        }
    }
    {
        const u8 *c = (const u8 *)T->tacticCall;
        if (memcmp(c - 17, kTacBefore, 10) || CallTarget(T->tacticCall - 8) != T->HandleToPtr ||
            memcmp(c - 3, kTacMid, 3) || c[0] != 0xE8 || CallTarget(T->tacticCall) != T->HandleToPtr ||
            memcmp(c + 5, kTacAfter, 7)) {
            FailMsg(changed, T->tacticCall);
            return;
        }
    }
    if (memcmp((void *)T->RegisterMetaData, T->proRegisterMetaData, 16) ||
        memcmp((void *)T->HandleToPtr, T->proHandleToPtr, 16) ||
        memcmp((void *)T->PtrToHandle, T->proPtrToHandle, 16) ||
        memcmp((void *)T->IsDerivedFrom, T->proIsDerivedFrom, 16) ||
        memcmp((void *)T->baseInit, T->proBaseInit, 16)) {
        FailMsg(L"the program code it relies on is different from what it expects", 0);
        return;
    }

    /* --- runtime state --------------------------------------------------- */
    g_T = T;
    g_HandleToPtr = (HandleToPtr_t)T->HandleToPtr;
    g_PtrToHandle = (PtrToHandle_t)T->PtrToHandle;
    g_IsDerivedFrom = (IsDerivedFrom_t)T->IsDerivedFrom;
    g_baseInit = (BaseInit_t)T->baseInit;

    /* --- two jump stubs near the program --------------------------------- */
    u8 *cave = AllocNear((u64)exe, soi);
    if (!cave) { FailMsg(L"could not reserve memory near the program code", 0); return; }
    u8 *c = cave;
    u64 stubInit = (u64)c;                 /* mov rax, AP_InitHook ; jmp rax */
    { u64 fn = (u64)&AP_InitHook; *c++ = 0x48; *c++ = 0xB8; memcpy(c, &fn, 8); c += 8; *c++ = 0xFF; *c++ = 0xE0; }
    c = (u8 *)(((u64)c + 15) & ~15ULL);
    u64 stubTactic = (u64)c;               /* mov rdx, rdi ; mov rax, AP_TacticMarkerHook ; jmp rax */
    { u64 fn = (u64)&AP_TacticMarkerHook; *c++ = 0x48; *c++ = 0x89; *c++ = 0xFA;
      *c++ = 0x48; *c++ = 0xB8; memcpy(c, &fn, 8); c += 8; *c++ = 0xFF; *c++ = 0xE0; }
    {
        u32 old;
        VirtualProtect(cave, 0x1000, PAGE_EXECUTE_READ, &old);
        FlushInstructionCache(GetCurrentProcess(), cave, 0x1000);
    }
    u8 callInit[5], callTactic[5];
    i32 rel;
    if (!Rel32(T->initCall + 5, stubInit, &rel)) goto far;
    callInit[0] = 0xE8; memcpy(callInit + 1, &rel, 4);
    if (!Rel32(T->tacticCall + 5, stubTactic, &rel)) goto far;
    callTactic[0] = 0xE8; memcpy(callTactic + 1, &rel, 4);

    /* --- apply: code first, the meta data last ---------------------------- */
    if (!Patch(T->propsCtorTail, kNewCtorTail, 27) ||
        !Patch(T->initCall, callInit, 5) ||
        !Patch(T->tacticCall, callTactic, 5)) {
        FailMsg(L"VirtualProtect on the program failed", 0);
        return;
    }

    u32 nq = BuildStream(src);
    u8 init[22];
    init[0] = 0x48; init[1] = 0xB9; { u64 a = (u64)g_stream; memcpy(init + 2, &a, 8); }       /* mov rcx, stream */
    init[10] = 0x48; init[11] = 0xB8; memcpy(init + 12, &T->RegisterMetaData, 8);               /* mov rax, RegisterMetaData */
    init[20] = 0xFF; init[21] = 0xE0;                                                            /* jmp rax */
    if (!Patch(T->staticInit, init, 22)) {
        FailMsg(L"VirtualProtect on the program failed", T->staticInit);
        return;
    }

    Log(L"applied: ");
    Log(T->exeName);
    LogHex(L"\n  meta data stream ", (u64)g_stream);
    Log(L" ("); wcatdec(g_log, 8192, nq); Log(L" qwords)");
    LogHex(L"\n  constructor end ", T->propsCtorTail);
    LogHex(L"\n  character init ", T->initCall); LogHex(L" -> ", stubInit);
    LogHex(L"\n  tactic start ", T->tacticCall); LogHex(L" -> ", stubTactic);
    Log(L"\n");
    return;

far:
    FailMsg(L"internal error: code out of range", (u64)cave);
}

int __stdcall DllMain(void *inst, u32 reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_plugPath, 520);
        const wchar_t *nm = g_plugPath;
        for (const wchar_t *p = g_plugPath; *p; p++)
            if (*p == L'\\' || *p == L'/') nm = p + 1;
        g_plugName[0] = 0;
        wcat(g_plugName, 128, nm);
        Apply();
        LogFlush();
    }
    return 1;
}
