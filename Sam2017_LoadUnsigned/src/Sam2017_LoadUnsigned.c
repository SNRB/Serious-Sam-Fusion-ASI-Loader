/*
 * Sam2017_LoadUnsigned.asi
 * ---------------------------------------------------------------------------
 * Lets the editor and the game load otherwise-valid assets that are missing
 * the editor signature, for Serious Sam Fusion 2017, builds of 7 June 2019
 * and 19 November 2026:
 *     Sam2017_SeriousEditor.exe  and  Sam2017_Unrestricted.exe
 *
 * Without this, loading such a file fails with
 *     "'<file>' isn't signed with sufficient credentials (EDITOR)"
 *     "Stream is signed with insufficient credentials."
 * and the asset is rejected.
 *
 * The signature check itself is left in place: the engine still parses the
 * SIGSTRM header and still verifies any signature that IS present (a present
 * but broken signature still fails, as before). Only the final "this stream
 * does not carry sufficient credentials" decision is changed so that a
 * missing or insufficient signature no longer blocks loading - the stream
 * continues down the same path a properly signed one would.
 *
 * How it works (all in memory, the .exe files are not modified):
 *   - In the stream-stack credential check, the test that turns "no
 *     sufficient signature found" into the two errors above is replaced by an
 *     unconditional jump to the check's normal success continuation.
 *   - A second gate (a virtual check on the current signature context that
 *     raises the same "insufficient credentials" error) has its conditional
 *     "proceed" branch made unconditional.
 *
 * It patches raw bytes, so it does not need to load before the engine starts,
 * but the same msimg32.dll ASI loader as the other Sam2017 mods is fine.
 * ---------------------------------------------------------------------------
 */

#include "sig_common.h"

#define IMPORT __declspec(dllimport)
IMPORT void *__stdcall GetModuleHandleW(const wchar_t *name);
IMPORT u32   __stdcall GetModuleFileNameW(void *mod, wchar_t *buf, u32 n);
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

#define PAGE_EXECUTE_READWRITE  0x40
#define GENERIC_WRITE           0x40000000
#define FILE_SHARE_READ         1
#define CREATE_ALWAYS           2
#define FILE_ATTRIBUTE_NORMAL   0x80
#define INVALID_HANDLE          ((void *)(i64)-1)
#define DLL_PROCESS_ATTACH      1

void *memcpy(void *d, const void *s, u64 n)
{
    u8 *p = (u8 *)d; const u8 *q = (const u8 *)s;
    while (n--) *p++ = *q++;
    return d;
}
int memcmp(const void *a, const void *b, u64 n)
{
    const u8 *p = (const u8 *)a, *q = (const u8 *)b;
    for (; n; n--, p++, q++) if (*p != *q) return *p < *q ? -1 : 1;
    return 0;
}

static const Target g_targets[] = {
#include "targets.inc"
};

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
    wchar_t b[20]; int i = 0; b[i++] = L'0'; b[i++] = L'x'; int started = 0;
    for (int sh = 60; sh >= 0; sh -= 4) {
        u32 nib = (u32)(v >> sh) & 15;
        if (nib || started || sh == 0) { b[i++] = (wchar_t)(nib < 10 ? L'0' + nib : L'A' + nib - 10); started = 1; }
    }
    b[i] = 0; wcat(d, cap, b);
}
static void wcatdec(wchar_t *d, u32 cap, u64 v)
{
    wchar_t b[24]; int i = 23; b[i] = 0;
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

static wchar_t g_plugPath[520];
static wchar_t g_plugName[128];
static wchar_t g_log[8192];

static void Log(const wchar_t *s) { wcat(g_log, 8192, s); }
static void LogHex(const wchar_t *s, u64 v) { Log(s); wcathex(g_log, 8192, v); }

static void LogFlush(void)
{
    wchar_t path[540]; path[0] = 0; wcat(path, 540, g_plugPath);
    u32 n = wlen(path);
    while (n && path[n - 1] != L'.' && path[n - 1] != L'\\' && path[n - 1] != L'/') n--;
    if (n && path[n - 1] == L'.') path[n - 1] = 0;
    wcat(path, 540, L".log");
    void *h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE) return;
    static char buf[8192]; u32 len = 0;
    for (u32 i = 0; g_log[i] && len < sizeof(buf); i++) {
        wchar_t c = g_log[i];
        if (c == L'\n') { if (len + 2 > sizeof(buf)) break; buf[len++] = '\r'; buf[len++] = '\n'; }
        else buf[len++] = (char)(c < 128 ? c : '?');
    }
    u32 wr; WriteFile(h, buf, len, &wr, 0); CloseHandle(h);
    OutputDebugStringW(g_log);
}

static void Message(const wchar_t *text)
{
    wchar_t var[64], tmp[4]; var[0] = 0;
    wcat(var, 64, L"SAM2017_LOADUNSIGNED_WARNED_");
    wcatdec(var, 64, GetCurrentProcessId());
    if (GetEnvironmentVariableW(var, tmp, 4)) return;
    SetEnvironmentVariableW(var, L"1");
    void *u = LoadLibraryW(L"user32.dll"); if (!u) return;
    typedef int (__stdcall *MB_t)(void *, const wchar_t *, const wchar_t *, u32);
    MB_t mb = (MB_t)GetProcAddress(u, "MessageBoxW");
    if (mb) mb(0, text, L"Sam2017 mods - Load unsigned assets", 0x30 /* MB_ICONWARNING */);
}

static void FailMsg(const wchar_t *why, u64 addr)
{
    static wchar_t m[1024]; m[0] = 0;
    wcat(m, 1024, g_plugName);
    wcat(m, 1024, L" was not applied to this program: ");
    wcat(m, 1024, why);
    if (addr) { wcat(m, 1024, L" (at "); wcathex(m, 1024, addr); wcat(m, 1024, L")"); }
    wcat(m, 1024, L".\n\nThe program continues unchanged.");
    Log(L"NOT APPLIED: "); Log(why); if (addr) LogHex(L" at ", addr); Log(L"\n");
    Message(m);
}

static int Patch(u64 addr, const void *bytes, u32 n)
{
    u32 old;
    if (!VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old)) {
        LogHex(L"VirtualProtect failed at ", addr); LogHex(L", error ", GetLastError()); Log(L"\n");
        return 0;
    }
    memcpy((void *)addr, bytes, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
    return 1;
}

static void Apply(void)
{
    u8 *exe = (u8 *)GetModuleHandleW(0);
    u8 *nt = exe + *(u32 *)(exe + 0x3c);
    u32 ts = *(u32 *)(nt + 8);
    u32 soi = *(u32 *)(nt + 24 + 56);

    wchar_t exePath[520]; exePath[0] = 0; GetModuleFileNameW(0, exePath, 520);
    const wchar_t *exeName = exePath;
    for (const wchar_t *p = exePath; *p; p++) if (*p == L'\\' || *p == L'/') exeName = p + 1;

    Log(L"Sam2017 mods - load assets missing the editor signature\n");
    Log(L"program: "); Log(exePath);
    LogHex(L"\nTimeDateStamp ", ts); LogHex(L", SizeOfImage ", soi); LogHex(L", base ", (u64)exe); Log(L"\n");

    const Target *T = 0;
    for (u32 i = 0; i < sizeof(g_targets) / sizeof(g_targets[0]); i++)
        if (g_targets[i].timeDateStamp == ts && g_targets[i].sizeOfImage == soi) T = &g_targets[i];
    if (!T) {
        for (u32 i = 0; i < sizeof(g_targets) / sizeof(g_targets[0]); i++)
            if (wieq(exeName, g_targets[i].exeName)) {
                static wchar_t m[1024]; m[0] = 0;
                wcat(m, 1024, exeName);
                wcat(m, 1024, L" is not one of the original executables (builds of 7 June 2019 and "
                              L"19 November 2026) that ");
                wcat(m, 1024, g_plugName);
                wcat(m, 1024, L" was made for. If it is a patched exe, restore the original one "
                              L"(Steam: Properties > Installed Files > Verify integrity of game files).\n\n"
                              L"The program continues unchanged.");
                Log(L"NOT APPLIED: unknown build of a supported program\n");
                Message(m);
                return;
            }
        Log(L"nothing to do: not a program this plugin knows\n");
        return;
    }
    if ((u64)exe != 0x140000000ULL) { FailMsg(L"unexpected image base", (u64)exe); return; }

    /* already patched? (another copy of this plugin) */
    if (memcmp((void *)T->mainSite, T->mainPatch, 9) == 0 &&
        memcmp((void *)T->secSite, T->secPatch, 2) == 0) {
        Log(L"already applied, skipped\n");
        return;
    }

    /* the code must be exactly what we expect, or we change nothing */
    if (memcmp((void *)T->mainSite, T->mainExpect, 9) != 0) {
        FailMsg(L"the signature-check code is different from what it expects. "
                L"Another mod may have changed it", T->mainSite);
        return;
    }
    if (memcmp((void *)(T->secSite - 6), T->secExpect, 8) != 0) {
        FailMsg(L"the signature-check code is different from what it expects. "
                L"Another mod may have changed it", T->secSite);
        return;
    }

    if (!Patch(T->mainSite, T->mainPatch, 9)) { FailMsg(L"VirtualProtect on the program failed", T->mainSite); return; }
    if (!Patch(T->secSite, T->secPatch, 2))  { FailMsg(L"VirtualProtect on the program failed", T->secSite); return; }

    Log(L"applied: "); Log(T->exeName);
    LogHex(L"\n  stream credential check ", T->mainSite); Log(L" -> load anyway");
    LogHex(L"\n  signature-context gate ", T->secSite); Log(L" -> load anyway");
    Log(L"\nAssets missing the editor signature now load. A present signature is still verified.\n");
}

int __stdcall DllMain(void *inst, u32 reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_plugPath, 520);
        const wchar_t *nm = g_plugPath;
        for (const wchar_t *p = g_plugPath; *p; p++) if (*p == L'\\' || *p == L'/') nm = p + 1;
        g_plugName[0] = 0; wcat(g_plugName, 128, nm);
        Apply();
        LogFlush();
    }
    return 1;
}
