/*
 * Sam2017_MultiSpawnerOrder.asi
 * ---------------------------------------------------------------------------
 * Adds a "Template order method" drop-down to the Multi spawner entity
 * (CMultiSpawnerProperties) of Serious Sam Fusion 2017, build of 7 June 2019:
 *     Sam2017_SeriousEditor.exe  and  Sam2017_Unrestricted.exe
 *
 *   Default            - unchanged: "Randomize template order" decides, exactly
 *                        as before (so existing levels behave the same).
 *   Groups             - every template spawns as one block of
 *                        <Spawn weight factor> monsters; the order of the blocks
 *                        is shuffled with the spawner's static random base
 *                        (ep_fRndBase), so it is the same on every run.
 *   Full random        - like "Randomize template order" (weight factor = how
 *                        often), but with a new seed on every run, and a new
 *                        shuffle each time the template list has been used up.
 *   Groups full random - Groups, with a new seed on every run (and cycle).
 *
 * How it works (all in memory, the .exe files are not modified):
 *   1. The Multi spawner's type description (meta data stream) is copied and
 *      registered instead of the original, with
 *        - a new enum type ETemplateOrderMethod,
 *        - a new member  ep_etomTemplateOrderMethod at offset 0x94 (that is
 *          4 bytes of padding at the end of the 0x98-byte object),
 *        - the type's format number raised from 5 to 6, so levels saved
 *          before still load (the engine converts older formats).
 *   2. The three places that construct CMultiSpawnerProperties also clear
 *      offset 0x94 (new spawners and old levels start as "Default").
 *   3. CSpawnerEntity's constructor also clears offset 0x14c (padding), used
 *      to hold the per-run seed of the "full random" methods.
 *   4. CMultiSpawnerProperties' virtual GetTemplate(iSlot, ctMax, ulDiff)
 *      (vtable + 0x130) is replaced; for "Default" it calls the original.
 *
 * Needs an ASI loader that loads plugins when the program starts, before the
 * engine registers its types (the msimg32.dll loader of the Sam2017 mods).
 * ---------------------------------------------------------------------------
 */

#include "mso_common.h"

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
IMPORT int   __stdcall QueryPerformanceCounter(i64 *v);
IMPORT u64   __stdcall GetTickCount64(void);
IMPORT u32   __stdcall GetCurrentProcessId(void);
IMPORT int   __stdcall DisableThreadLibraryCalls(void *mod);
IMPORT u32   __stdcall GetLastError(void);
IMPORT u32   __stdcall GetEnvironmentVariableW(const wchar_t *name, wchar_t *buf, u32 n);
IMPORT int   __stdcall SetEnvironmentVariableW(const wchar_t *name, const wchar_t *value);
IMPORT void  __stdcall OutputDebugStringW(const wchar_t *s);

#define MEM_COMMIT              0x1000
#define MEM_RESERVE             0x2000
#define PAGE_READWRITE          0x04
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
    wcat(var, 64, L"SAM2017_MSO_WARNED_");
    wcatdec(var, 64, GetCurrentProcessId());
    if (GetEnvironmentVariableW(var, tmp, 4)) return;
    SetEnvironmentVariableW(var, L"1");
    void *u32dll = LoadLibraryW(L"user32.dll");
    if (!u32dll) return;
    typedef int (__stdcall *MessageBoxW_t)(void *, const wchar_t *, const wchar_t *, u32);
    MessageBoxW_t mb = (MessageBoxW_t)GetProcAddress(u32dll, "MessageBoxW");
    if (mb) mb(0, text, L"Sam2017 mods - Multi spawner order", 0x30 /* MB_ICONWARNING */);
}

#include "mso_logic.h"

/* ------------------------------------------------------------------------- */
/* patching                                                                  */
/* ------------------------------------------------------------------------- */
static u64 Fnv1a64(const u8 *p, u64 n)
{
    u64 h = 0xcbf29ce484222325ULL;
    while (n--) { h ^= *p++; h *= 0x100000001b3ULL; }
    return h;
}

static int Patch(u64 addr, const void *bytes, u32 n, u32 prot)
{
    u32 old;
    if (!VirtualProtect((void *)addr, n, prot, &old)) {
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

/* jmp rel32 at 'site' (padded with NOPs to 'len' bytes) */
static int MakeJmp(u8 *buf, u64 site, u64 to, u32 len)
{
    i32 rel;
    if (!Rel32(site + 5, to, &rel)) return 0;
    buf[0] = 0xE9;
    memcpy(buf + 1, &rel, 4);
    for (u32 i = 5; i < len; i++) buf[i] = 0x90;
    return 1;
}

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

    Log(L"Sam2017 mods - Multi spawner template order methods\n");
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
                wcat(m, 1024, L" is not the original executable of 7 June 2019 that ");
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
    for (u32 i = 21; i < 32; i++)
        if (si[i] != 0xCC) { FailMsg(L"unexpected program code", T->staticInit + i); return; }

    if (*(u64 *)T->classGlobal != 0) {
        static wchar_t m[1024];
        m[0] = 0;
        wcat(m, 1024, g_plugName);
        wcat(m, 1024, L" was loaded after the program had started, too late for its changes. "
                      L"Install the msimg32.dll loader that comes with the Sam2017 mods (copy msimg32.dll "
                      L"next to the exe, with this plugin in the plugins folder), or use an ASI loader that "
                      L"loads plugins before the program starts.\n\nThe program continues without this mod.");
        Log(L"NOT APPLIED: loaded too late (Multi spawner type already registered)\n");
        Message(m);
        return;
    }

    if (Fnv1a64((const u8 *)T->stream, STREAM_QWORDS * 8) != T->streamHash) {
        FailMsg(L"the Multi spawner meta data is different from what it expects", T->stream);
        return;
    }
    const u64 *src = (const u64 *)T->stream;
    if (src[IDX_FORMAT] != 5 || src[IDX_NMEMBERS] != 4 || src[IDX_BOOLREC] != 1 ||
        src[IDX_BOOLREC + 2] != 3 || src[IDX_BOOLREC + 7] != 0x8c || src[STREAM_QWORDS - 1] != ~0ULL) {
        FailMsg(L"unexpected meta data layout", T->stream);
        return;
    }

    static const u8 mov8c[11] = {0x48, 0xC7, 0x83, 0x8C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    static const u8 mov0c[8]  = {0x48, 0xC7, 0x43, 0x0C, 0x01, 0x00, 0x00, 0x00};
    static const u8 tail[6]   = {0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
    if (memcmp((void *)T->ctorInplace, mov8c, 11)) { FailMsg(L"the program code it changes was already changed by something else", T->ctorInplace); return; }
    if (memcmp((void *)T->ctorNew, mov8c, 11))     { FailMsg(L"the program code it changes was already changed by something else", T->ctorNew); return; }
    if (memcmp((void *)T->ctorArray, mov0c, 8))    { FailMsg(L"the program code it changes was already changed by something else", T->ctorArray); return; }
    if (memcmp((void *)T->spawnerCtorTail, tail, 6)) { FailMsg(L"the program code it changes was already changed by something else", T->spawnerCtorTail); return; }

    u64 slot = T->mspVtable + 0x130;
    if (*(u64 *)slot != T->origGetTemplate) { FailMsg(L"the Multi spawner was already changed by something else", slot); return; }
    if (memcmp((void *)T->RegisterMetaData, T->proRegisterMetaData, 16) ||
        memcmp((void *)T->HandleToPtr, T->proHandleToPtr, 16) ||
        memcmp((void *)T->PtrToHandle, T->proPtrToHandle, 16) ||
        memcmp((void *)T->Warn, T->proWarn, 16) ||
        memcmp((void *)T->origGetTemplate, T->proGetTemplate, 16)) {
        FailMsg(L"the program code it relies on is different from what it expects", 0);
        return;
    }

    /* --- runtime state --------------------------------------------------- */
    g_T = T;
    g_origGetTemplate = (GetTemplate_t)T->origGetTemplate;
    g_HandleToPtr = (HandleToPtr_t)T->HandleToPtr;
    g_PtrToHandle = (PtrToHandle_t)T->PtrToHandle;
    g_Warn = (Warn_t)T->Warn;
    {
        i64 qpc = 0;
        QueryPerformanceCounter(&qpc);
        g_rng = Mix64((u64)qpc ^ (GetTickCount64() << 20) ^ ((u64)GetCurrentProcessId() << 40) ^
                      __builtin_ia32_rdtsc() ^ (u64)&qpc);
        g_fallbackSeed = (u32)SplitMix(&g_rng) | 1;
    }

    /* --- code near the program for the constructor hooks ----------------- */
    u8 *cave = AllocNear((u64)exe, soi);
    if (!cave) { FailMsg(L"could not reserve memory near the program code", 0); return; }
    u8 *c = cave;
    i32 rel;
    u64 t1 = (u64)c;   /* in-place ctor */
    memcpy(c, mov8c, 11); c += 11;
    { static const u8 z[10] = {0xC7, 0x83, 0x94, 0x00, 0x00, 0x00, 0, 0, 0, 0}; memcpy(c, z, 10); c += 10; }
    if (!Rel32((u64)c + 5, T->ctorInplace + 11, &rel)) goto far;
    *c++ = 0xE9; memcpy(c, &rel, 4); c += 4;
    c = (u8 *)(((u64)c + 15) & ~15ULL);
    u64 t2 = (u64)c;   /* New (single object) */
    memcpy(c, mov8c, 11); c += 11;
    { static const u8 z[10] = {0xC7, 0x83, 0x94, 0x00, 0x00, 0x00, 0, 0, 0, 0}; memcpy(c, z, 10); c += 10; }
    if (!Rel32((u64)c + 5, T->ctorNew + 11, &rel)) goto far;
    *c++ = 0xE9; memcpy(c, &rel, 4); c += 4;
    c = (u8 *)(((u64)c + 15) & ~15ULL);
    u64 t3 = (u64)c;   /* New (array): rbx = object + 0x80 */
    memcpy(c, mov0c, 8); c += 8;
    { static const u8 z[7] = {0xC7, 0x43, 0x14, 0, 0, 0, 0}; memcpy(c, z, 7); c += 7; }
    if (!Rel32((u64)c + 5, T->ctorArray + 8, &rel)) goto far;
    *c++ = 0xE9; memcpy(c, &rel, 4); c += 4;
    c = (u8 *)(((u64)c + 15) & ~15ULL);
    u64 t4 = (u64)c;   /* CSpawnerEntity ctor end */
    { static const u8 z[10] = {0xC7, 0x83, 0x4C, 0x01, 0x00, 0x00, 0, 0, 0, 0}; memcpy(c, z, 10); c += 10; }
    memcpy(c, tail, 6); c += 6;
    {
        u32 old;
        VirtualProtect(cave, 0x1000, PAGE_EXECUTE_READ, &old);
        FlushInstructionCache(GetCurrentProcess(), cave, 0x1000);
    }

    u8 j1[11], j2[11], j3[8], j4[6];
    if (!MakeJmp(j1, T->ctorInplace, t1, 11) || !MakeJmp(j2, T->ctorNew, t2, 11) ||
        !MakeJmp(j3, T->ctorArray, t3, 8) || !MakeJmp(j4, T->spawnerCtorTail, t4, 6))
        goto far;

    /* --- apply: constructors and GetTemplate first, the meta data last --- */
    u64 fn = (u64)&MSO_GetTemplate;
    if (!Patch(T->ctorInplace, j1, 11, PAGE_EXECUTE_READWRITE) ||
        !Patch(T->ctorNew, j2, 11, PAGE_EXECUTE_READWRITE) ||
        !Patch(T->ctorArray, j3, 8, PAGE_EXECUTE_READWRITE) ||
        !Patch(T->spawnerCtorTail, j4, 6, PAGE_EXECUTE_READWRITE) ||
        !Patch(slot, &fn, 8, PAGE_READWRITE)) {
        FailMsg(L"VirtualProtect on the program failed", 0);
        return;
    }

    u32 nq = BuildStream(src);
    u8 init[22];
    init[0] = 0x48; init[1] = 0xB9; { u64 a = (u64)g_stream; memcpy(init + 2, &a, 8); }       /* mov rcx, stream */
    init[10] = 0x48; init[11] = 0xB8; memcpy(init + 12, &T->RegisterMetaData, 8);               /* mov rax, RegisterMetaData */
    init[20] = 0xFF; init[21] = 0xE0;                                                            /* jmp rax */
    if (!Patch(T->staticInit, init, 22, PAGE_EXECUTE_READWRITE)) {
        FailMsg(L"VirtualProtect on the program failed", T->staticInit);
        return;
    }

    Log(L"applied: ");
    Log(T->exeName);
    LogHex(L"\n  meta data stream ", (u64)g_stream);
    Log(L" ("); wcatdec(g_log, 8192, nq); Log(L" qwords)");
    LogHex(L"\n  GetTemplate ", slot); LogHex(L" -> ", fn);
    LogHex(L"\n  constructor hooks at ", (u64)cave);
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
