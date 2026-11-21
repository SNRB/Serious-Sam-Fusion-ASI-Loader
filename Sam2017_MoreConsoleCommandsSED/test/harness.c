/*
 * Test harness for Sam2017_MoreConsoleCommandsSED.asi (Linux x86-64).
 *
 * Maps the real Sam2017_SeriousEditor.exe image at its fixed base 0x140000000,
 * loads the plugin DLL with a fake kernel32, calls its DllMain, then executes
 * the editor's own code paths that reach the redirected calls:
 *   - the Core module initialization (0x140B589C0) -> RegisterDeclarations,
 *     then cvar assignment, the console's script write handler, and the editor's
 *     own cvar saving to / loading from SeriousEditor.ini (file stream faked);
 *   - the console help: list of cvars (0x14075B0D0), "help" command (0x14075B260),
 *     the lookup used by the console tooltip (0x14075C060), each reaching the cvar
 *     help loader through a redirected call, then the tooltip builder (0x14075BBA0);
 *   - the editor startup's call that fetches CommandHandler.Global (0x140160B48),
 *     with the editor's real command handler and RegisterCommand/RegisterShortcut
 *     (mode realcmd) or with recording fakes (mode full), and the command itself.
 * Editor imports are trap stubs (reported by name) unless emulated below.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <asm/prctl.h>

#define MSABI __attribute__((ms_abi))
typedef uint16_t WCHAR;
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef long long s64;

#define EXE_BASE 0x140000000ULL

static u8 *g_exe = (u8 *)EXE_BASE;
static u32 g_exeSize;
static u8 *g_plugin;
static u32 g_pluginSize;
static const char *g_mode = "full";
static int g_failures;

#define CHECK(cond, ...) do { if (cond) { printf("  ok   "); } else { printf("  FAIL "); g_failures++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static void *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
    void *p = malloc(*size);
    if (fread(p, 1, *size, f) != *size) { perror("fread"); exit(2); }
    fclose(f);
    return p;
}

static void wstr_to_utf8(const WCHAR *w, char *out, size_t cap)
{
    size_t n = 0;
    if (!w) { snprintf(out, cap, "(null)"); return; }
    while (*w && n + 4 < cap) {
        u32 c = *w++;
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { out[n++] = 0xC0 | (c >> 6); out[n++] = 0x80 | (c & 63); }
        else { out[n++] = 0xE0 | (c >> 12); out[n++] = 0x80 | ((c >> 6) & 63); out[n++] = 0x80 | (c & 63); }
    }
    out[n] = 0;
}

static size_t wlen(const WCHAR *w) { size_t n = 0; while (w[n]) n++; return n; }

static void utf8_to_w(const char *s, WCHAR *out, size_t cap)
{
    size_t n = 0;
    while (*s && n + 1 < cap) out[n++] = (u8)*s++;
    out[n] = 0;
}

/* ------------------------------------------------------------------ */
/* PE mapping                                                          */
/* ------------------------------------------------------------------ */

static u32 pe_nt(const u8 *b) { return *(const u32 *)(b + 0x3C); }

static void map_image(const u8 *file, u8 *base, u32 *sizeOut)
{
    u32 nt = pe_nt(file);
    u16 nsec = *(const u16 *)(file + nt + 6);
    u16 optsz = *(const u16 *)(file + nt + 20);
    u32 sizeOfImage = *(const u32 *)(file + nt + 24 + 56);
    u32 sizeOfHeaders = *(const u32 *)(file + nt + 24 + 60);
    memcpy(base, file, sizeOfHeaders);
    const u8 *sh = file + nt + 24 + optsz;
    for (int i = 0; i < nsec; i++) {
        u32 vsz = *(const u32 *)(sh + 40 * i + 8), rva = *(const u32 *)(sh + 40 * i + 12);
        u32 rsz = *(const u32 *)(sh + 40 * i + 16), raw = *(const u32 *)(sh + 40 * i + 20);
        u32 n = rsz < vsz ? rsz : vsz;
        if (vsz == 0) n = rsz;
        memcpy(base + rva, file + raw, n);
    }
    *sizeOut = sizeOfImage;
}

static u32 dir_rva(const u8 *base, int idx, u32 *size)
{
    u32 nt = pe_nt(base);
    const u8 *opt = base + nt + 24;
    if (size) *size = *(const u32 *)(opt + 112 + 8 * idx + 4);
    return *(const u32 *)(opt + 112 + 8 * idx);
}

typedef void *(*ImportResolver)(const char *dll, const char *fn, int index);

static void resolve_imports(u8 *base, ImportResolver resolve)
{
    u32 rva = dir_rva(base, 1, 0);
    int index = 0;
    for (u8 *d = base + rva; ; d += 20) {
        u32 ilt = *(u32 *)d, name = *(u32 *)(d + 12), iat = *(u32 *)(d + 16);
        if (!ilt && !name && !iat) break;
        const char *dll = (const char *)base + name;
        u64 *thunk = (u64 *)(base + (ilt ? ilt : iat));
        u64 *slot = (u64 *)(base + iat);
        for (int k = 0; thunk[k]; k++, index++) {
            static char ord[32];
            const char *fn;
            if (thunk[k] >> 63) { snprintf(ord, sizeof ord, "#%u", (unsigned)(thunk[k] & 0xFFFF)); fn = ord; }
            else fn = (const char *)base + (u32)thunk[k] + 2;
            slot[k] = (u64)resolve(dll, fn, index);
        }
    }
}

static void apply_relocs2(u8 *base, s64 delta)
{
    u32 size, rva = dir_rva(base, 5, &size);
    u8 *p = base + rva, *end = p + size;
    while (p < end) {
        u32 page = *(u32 *)p, bsz = *(u32 *)(p + 4);
        if (!bsz) break;
        for (u32 i = 8; i < bsz; i += 2) {
            u16 e = *(u16 *)(p + i);
            if ((e >> 12) == 10) *(u64 *)(base + page + (e & 0xFFF)) += (u64)delta;
            else if ((e >> 12) != 0) { printf("unsupported reloc type %d\n", e >> 12); exit(2); }
        }
        p += bsz;
    }
}

/* ------------------------------------------------------------------ */
/* Editor imports: traps + the few emulated functions                  */
/* ------------------------------------------------------------------ */

static const char *g_impName[1024];
static u8 *g_thunks;

MSABI static void trap_report(int idx, u64 ret)
{
    printf("TRAP: the editor called import %s from %#lx (not emulated by the harness)\n", g_impName[idx], ret);
    fflush(stdout);
    _exit(3);
}

static u32 g_lastError;
static void *g_tlsValues[64];
static int g_tlsNext = 1;
MSABI static u32 e_GetCurrentThreadId(void) { return 1; }
MSABI static u32 e_GetCurrentProcessId(void) { return 2; }
MSABI static void e_NopCS(void *cs) { (void)cs; }
MSABI static int e_TryEnterCS(void *cs) { (void)cs; return 1; }
MSABI static int e_InitCSSpin(void *cs, u32 spin) { (void)cs; (void)spin; return 1; }
MSABI static u32 e_TlsAlloc(void) { return g_tlsNext++; }
MSABI static void *e_TlsGetValue(u32 i) { return i < 64 ? g_tlsValues[i] : 0; }
MSABI static int e_TlsSetValue(u32 i, void *v) { if (i < 64) g_tlsValues[i] = v; return 1; }
MSABI static void *e_GetProcessHeap(void) { return (void *)1; }
MSABI static void *e_HeapAlloc(void *h, u32 f, size_t n) { (void)h; return (f & 8) ? calloc(1, n ? n : 1) : malloc(n ? n : 1); }
MSABI static int e_HeapFree(void *h, u32 f, void *p) { (void)h; (void)f; free(p); return 1; }
MSABI static void *e_HeapReAlloc(void *h, u32 f, void *p, size_t n) { (void)h; (void)f; return realloc(p, n); }
MSABI static u32 e_GetLastError(void) { return g_lastError; }
MSABI static void e_SetLastError(u32 e) { g_lastError = e; }
MSABI static void e_Sleep(u32 ms) { (void)ms; }
MSABI static int e_SwitchToThread(void) { return 0; }
MSABI static int e_QPC(s64 *v) { static s64 t = 1000; *v = (t += 1000); return 1; }
MSABI static int e_QPF(s64 *v) { *v = 10000000; return 1; }
MSABI static u32 e_GetTickCount(void) { return 12345; }
MSABI static void e_GetSystemTimeAsFileTime(u64 *ft) { *ft = 132000000000000000ULL; }
MSABI static int e_SystemTimeToFileTime(const void *st, u64 *ft) { (void)st; *ft = 129000000000000000ULL; return 1; }
MSABI static int e_IsDebuggerPresent(void) { return 0; }
MSABI static void e_OutputDebugStringA(const char *s) { printf("[editor debug] %s", s); }
MSABI static void e_OutputDebugStringW(const WCHAR *s) { char b[2048]; wstr_to_utf8(s, b, sizeof b); printf("[editor debug] %s", b); }

static void *editor_import(const char *dll, const char *fn, int index)
{
    static const struct { const char *name; void *fn; } emu[] = {
        { "GetCurrentThreadId", (void *)e_GetCurrentThreadId },
        { "GetCurrentProcessId", (void *)e_GetCurrentProcessId },
        { "EnterCriticalSection", (void *)e_NopCS }, { "LeaveCriticalSection", (void *)e_NopCS },
        { "InitializeCriticalSection", (void *)e_NopCS }, { "DeleteCriticalSection", (void *)e_NopCS },
        { "TryEnterCriticalSection", (void *)e_TryEnterCS },
        { "InitializeCriticalSectionAndSpinCount", (void *)e_InitCSSpin },
        { "TlsAlloc", (void *)e_TlsAlloc }, { "TlsGetValue", (void *)e_TlsGetValue }, { "TlsSetValue", (void *)e_TlsSetValue },
        { "GetProcessHeap", (void *)e_GetProcessHeap }, { "HeapAlloc", (void *)e_HeapAlloc },
        { "HeapFree", (void *)e_HeapFree }, { "HeapReAlloc", (void *)e_HeapReAlloc },
        { "GetLastError", (void *)e_GetLastError }, { "SetLastError", (void *)e_SetLastError },
        { "Sleep", (void *)e_Sleep }, { "SwitchToThread", (void *)e_SwitchToThread },
        { "QueryPerformanceCounter", (void *)e_QPC }, { "QueryPerformanceFrequency", (void *)e_QPF },
        { "GetTickCount", (void *)e_GetTickCount }, { "GetSystemTimeAsFileTime", (void *)e_GetSystemTimeAsFileTime },
        { "SystemTimeToFileTime", (void *)e_SystemTimeToFileTime }, { "IsDebuggerPresent", (void *)e_IsDebuggerPresent },
        { "OutputDebugStringA", (void *)e_OutputDebugStringA }, { "OutputDebugStringW", (void *)e_OutputDebugStringW },
    };
    (void)dll;
    for (size_t i = 0; i < sizeof emu / sizeof emu[0]; i++)
        if (!strcmp(fn, emu[i].name)) return emu[i].fn;
    g_impName[index] = strdup(fn);
    u8 *t = g_thunks + 32 * index;
    t[0] = 0xB9; *(u32 *)(t + 1) = (u32)index;               /* mov ecx, index */
    t[5] = 0x48; t[6] = 0x8B; t[7] = 0x14; t[8] = 0x24;        /* mov rdx, [rsp] */
    t[9] = 0x48; t[10] = 0xB8; *(u64 *)(t + 11) = (u64)trap_report; /* movabs rax, trap_report */
    t[19] = 0xFF; t[20] = 0xE0;                                /* jmp rax */
    return t;
}

/* ------------------------------------------------------------------ */
/* Engine memory manager stand-ins (malloc with a size header)         */
/* ------------------------------------------------------------------ */

#define HDR_MAGIC 0xA110CA7EULL
static long g_liveAllocs;
static void *fa_alloc(size_t n) { u64 *p = malloc(n + 16); p[0] = n; p[1] = HDR_MAGIC; g_liveAllocs++; return p + 2; }
static void fa_free(void *q)
{
    if (!q) return;
    u64 *p = (u64 *)q - 2;
    if (p[1] != HDR_MAGIC) return;   /* not from these stand-ins (CRT path): leak it */
    p[1] = 0; g_liveAllocs--; free(p);
}
static size_t fa_size(void *q) { return q ? ((u64 *)q)[-2] : 0; }
MSABI static void *m_alloc_tag(int n, void *tag) { (void)tag; return fa_alloc((size_t)(unsigned)n); }
MSABI static void *m_alloc3(int n, int x, void *tag) { (void)x; (void)tag; return fa_alloc((size_t)(unsigned)n); }
MSABI static void *m_alloc_flags(size_t n, int flags) { (void)flags; return fa_alloc(n); }
MSABI static void m_free(void *p) { fa_free(p); }
MSABI static void m_free_tag(void *p, void *tag) { (void)tag; fa_free(p); }
MSABI static size_t m_size(void *p) { return fa_size(p); }
MSABI static void *m_calloc(size_t count, size_t size) { void *p = fa_alloc(count * size); memset(p, 0, count * size); return p; }
MSABI static void *m_realloc(void *p, size_t n)
{
    void *r = fa_alloc(n);
    if (p) { size_t o = fa_size(p); memcpy(r, p, o < n ? o : n); fa_free(p); }
    return r;
}
MSABI static void m_unexpected(void)
{
    printf("TRAP: engine memory manager internals reached from %p (an allocation entry point is not emulated)\n",
           __builtin_return_address(0));
    fflush(stdout); _exit(3);
}

static void write_jmp(u64 at, void *fn)
{
    u8 *p = (u8 *)at;
    p[0] = 0x48; p[1] = 0xB8; *(u64 *)(p + 2) = (u64)fn; p[10] = 0xFF; p[11] = 0xE0;
}

/* Everything the editor prints (console and log) ends up here. */
static char g_edLog[1 << 18];
static size_t g_edLogLen;
static int g_quietLog;
MSABI static void editor_log_sink(int level, const char *text)
{
    size_t n = strlen(text);
    if (!g_quietLog) printf("  editor log (%d): %s%s", level, text, (n && text[n - 1] == '\n') ? "" : "\n");
    if (g_edLogLen + n + 16 < sizeof g_edLog) g_edLogLen += sprintf(g_edLog + g_edLogLen, "[%d]%s", level, text);
}
static int edlog_has(const char *s) { return strstr(g_edLog, s) != 0; }
static void edlog_clear(void) { g_edLogLen = 0; g_edLog[0] = 0; }

/* CString duplication (0x140B27910) with NULL treated as "": the C runtime's static
   constructors (which give global CStrings their value) do not run in this harness. */
MSABI static char *h_strdup(const char *s)
{
    if (!s) s = "";
    size_t n = strlen(s);
    if (!n) { (*(int *)0x1431DD2C0ULL)++; return (char *)0x1429709A8ULL; }
    char *p = fa_alloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

static void install_memory_standins(void)
{
    write_jmp(0x140B27910, (void *)h_strdup);
    write_jmp(0x140B20580, (void *)editor_log_sink);
    write_jmp(0x140B20EF0, (void *)m_alloc_tag);
    write_jmp(0x140B20E30, (void *)m_alloc3);
    write_jmp(0x140B216A0, (void *)m_alloc_flags);   /* 0x140B21690 jumps here */
    write_jmp(0x140B21260, (void *)m_free);
    write_jmp(0x140B21360, (void *)m_free_tag);
    write_jmp(0x140B217A0, (void *)m_size);
    write_jmp(0x140B21BC0, (void *)m_realloc);
    write_jmp(0x140B219D0, (void *)m_free_tag);
    write_jmp(0x140B210F0, (void *)m_calloc);
    write_jmp(0x140B20A00, (void *)m_unexpected);    /* allocator state: must not be reached */
}

/* ------------------------------------------------------------------ */
/* Fake kernel32 for the plugin                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    void *BaseAddress; void *AllocationBase; u32 AllocationProtect; u16 PartitionId; u16 pad0;
    size_t RegionSize; u32 State; u32 Protect; u32 Type; u32 pad1;
} MBI;

static WCHAR g_exePathW[512], g_selfPathW[512];
static struct { WCHAR name[64]; WCHAR val[64]; int set; } g_env[16];
static char g_logText[65536];
static size_t g_logLen;
static int g_boxes;
static char g_lastBox[8192];
static struct { u64 addr; size_t size; u32 prot; } g_prot[256];
static int g_nprot;
static void *g_allocAt[16];
static int g_nalloc;
static int g_pinned;

static int env_find(const WCHAR *n)
{
    for (int i = 0; i < 16; i++) if (g_env[i].set) {
        const WCHAR *a = g_env[i].name, *b = n;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) return i;
    }
    return -1;
}
static void env_set(const char *name, const char *val)
{
    WCHAR n[64]; utf8_to_w(name, n, 64);
    int i = env_find(n);
    if (!val) { if (i >= 0) g_env[i].set = 0; return; }
    if (i < 0) for (i = 0; i < 16 && g_env[i].set; i++) ;
    memcpy(g_env[i].name, n, sizeof n); utf8_to_w(val, g_env[i].val, 64); g_env[i].set = 1;
}

MSABI static int k_DisableThreadLibraryCalls(void *h) { (void)h; return 1; }
MSABI static void *k_GetModuleHandleW(const WCHAR *n) { return n ? 0 : (void *)g_exe; }
MSABI static int k_GetModuleHandleExW(u32 f, const WCHAR *n, void **out)
{
    if ((f & 4) && (u8 *)n >= g_plugin && (u8 *)n < g_plugin + g_pluginSize && (f & 1)) g_pinned = 1;
    *out = g_plugin; return 1;
}
MSABI static u32 k_GetModuleFileNameW(void *h, WCHAR *buf, u32 n)
{
    const WCHAR *src = h ? g_selfPathW : g_exePathW;
    u32 len = (u32)wlen(src);
    if (len + 1 > n) return 0;
    memcpy(buf, src, (len + 1) * 2); return len;
}
MSABI static u32 k_GetEnvironmentVariableW(const WCHAR *name, WCHAR *buf, u32 n)
{
    int i = env_find(name);
    if (i < 0) return 0;
    u32 len = (u32)wlen(g_env[i].val);
    if (len + 1 > n) return len + 1;
    memcpy(buf, g_env[i].val, (len + 1) * 2); return len;
}
MSABI static int k_SetEnvironmentVariableW(const WCHAR *name, const WCHAR *val)
{
    char n[128], v[128]; wstr_to_utf8(name, n, sizeof n);
    if (val) wstr_to_utf8(val, v, sizeof v);
    env_set(n, val ? v : 0); return 1;
}
MSABI static void *k_VirtualAlloc(void *addr, size_t size, u32 type, u32 prot)
{
    (void)type; (void)prot;
    void *p = mmap(addr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | (addr ? MAP_FIXED_NOREPLACE : 0), -1, 0);
    if (p == MAP_FAILED || (addr && p != addr)) { if (p != MAP_FAILED) munmap(p, size); g_lastError = 487; return 0; }
    if (g_nalloc < 16) g_allocAt[g_nalloc++] = p;
    return p;
}
MSABI static int k_VirtualFree(void *addr, size_t size, u32 type) { (void)size; (void)type; munmap(addr, 0x10000); return 1; }
MSABI static int k_VirtualProtect(void *addr, size_t size, u32 prot, u32 *old)
{
    u64 a = (u64)addr;
    u32 guess = 0x04;
    if (a >= EXE_BASE + 0x1000 && a < EXE_BASE + 0x1B54000) guess = 0x20;          /* .text  */
    else if (a >= EXE_BASE + 0x1B54000 && a < EXE_BASE + 0x286A000) guess = 0x02;  /* .rdata */
    for (int i = g_nprot - 1; i >= 0; i--)
        if (g_prot[i].addr <= a && a < g_prot[i].addr + g_prot[i].size) { guess = g_prot[i].prot; break; }
    if (g_nprot < 256) { g_prot[g_nprot].addr = a; g_prot[g_nprot].size = size; g_prot[g_nprot].prot = prot; g_nprot++; }
    *old = guess; return 1;
}
static int maps_lookup(u64 addr, u64 *start, u64 *end, u64 *nextStart, u64 *prevEnd)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512]; u64 s, e, prevE = 0; int found = 0;
    *nextStart = ~0ULL; *prevEnd = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%lx-%lx", &s, &e) != 2) continue;
        if (addr >= s && addr < e) { *start = s; *end = e; found = 1; }
        if (s > addr && s < *nextStart) *nextStart = s;
        if (e <= addr && e > prevE) prevE = e;
    }
    fclose(f);
    *prevEnd = prevE;
    return found;
}
MSABI static size_t k_VirtualQuery(const void *addr, MBI *mbi, size_t len)
{
    u64 a = (u64)addr & ~0xFFFULL, s, e, ns, pe;
    memset(mbi, 0, sizeof *mbi);
    mbi->BaseAddress = (void *)a;
    if (a >= EXE_BASE && a < EXE_BASE + g_exeSize) {
        /* the editor image: one page at a time, with the protection Windows gives each section */
        u32 prot = 0x04;                                                         /* .data etc. */
        if (a < EXE_BASE + 0x1000) prot = 0x02;                                  /* headers */
        else if (a < EXE_BASE + 0x1B54000) prot = 0x20;                          /* .text  */
        else if (a < EXE_BASE + 0x286A000) prot = 0x02;                          /* .rdata */
        else if (a >= EXE_BASE + 0x3324000) prot = 0x02;                         /* .pdata, .rsrc... */
        for (int i = g_nprot - 1; i >= 0; i--)
            if ((g_prot[i].addr & ~0xFFFULL) <= a && a < g_prot[i].addr + g_prot[i].size) { prot = g_prot[i].prot; break; }
        mbi->AllocationBase = (void *)EXE_BASE; mbi->RegionSize = 0x1000; mbi->State = 0x1000; mbi->Protect = prot;
    } else if (maps_lookup(a, &s, &e, &ns, &pe)) {
        mbi->AllocationBase = (void *)s; mbi->RegionSize = e - a; mbi->State = 0x1000; mbi->Protect = 0x40;
    } else {
        mbi->AllocationBase = 0; mbi->State = 0x10000; mbi->Protect = 0x01;
        mbi->RegionSize = (ns == ~0ULL ? 0x7FFFFFFF0000ULL : ns) - a;
    }
    (void)len;
    return sizeof *mbi;
}
static int g_flushes;
MSABI static int k_FlushInstructionCache(void *p, const void *a, size_t n) { (void)p; (void)a; (void)n; g_flushes++; return 1; }
MSABI static void *k_GetCurrentProcess(void) { return (void *)(intptr_t)-1; }
MSABI static u32 k_GetLastError(void) { return g_lastError; }
MSABI static void *k_CreateFileW(const WCHAR *name, u32 acc, u32 share, void *sa, u32 disp, u32 attr, void *t)
{
    static char logName[512];
    (void)acc; (void)share; (void)sa; (void)disp; (void)attr; (void)t;
    wstr_to_utf8(name, logName, sizeof logName);
    if (!strstr(logName, "Sam2017_SeriousEditor.asi.log")) printf("  !! log file name: %s\n", logName);
    return (void *)0x1234;
}
MSABI static int k_WriteFile(void *h, const void *buf, u32 n, u32 *written, void *ov)
{
    (void)h; (void)ov;
    if (g_logLen + n < sizeof g_logText) { memcpy(g_logText + g_logLen, buf, n); g_logLen += n; g_logText[g_logLen] = 0; }
    printf("  log> %.*s", (int)n, (const char *)buf);
    *written = n; return 1;
}
MSABI static int k_CloseHandle(void *h) { (void)h; return 1; }
MSABI static void k_OutputDebugStringW(const WCHAR *s) { (void)s; }
MSABI static void *k_LoadLibraryW(const WCHAR *n) { (void)n; return (void *)0x7777; }
MSABI static int k_MessageBoxW(void *h, const WCHAR *text, const WCHAR *cap, u32 type)
{
    char t[8192], c[256]; (void)h;
    wstr_to_utf8(text, t, sizeof t); wstr_to_utf8(cap, c, sizeof c);
    g_boxes++; snprintf(g_lastBox, sizeof g_lastBox, "%s", t);
    printf("  MESSAGE BOX [%s] (type 0x%x):\n    %s\n", c, type, t);
    return 1;
}
MSABI static void *k_GetProcAddress(void *h, const char *name)
{
    if (h == (void *)0x7777 && !strcmp(name, "MessageBoxW")) return (void *)k_MessageBoxW;
    return 0;
}

static void *plugin_import(const char *dll, const char *fn, int index)
{
    static const struct { const char *name; void *fn; } k[] = {
        { "DisableThreadLibraryCalls", (void *)k_DisableThreadLibraryCalls },
        { "GetModuleHandleW", (void *)k_GetModuleHandleW }, { "GetModuleHandleExW", (void *)k_GetModuleHandleExW },
        { "GetModuleFileNameW", (void *)k_GetModuleFileNameW },
        { "GetEnvironmentVariableW", (void *)k_GetEnvironmentVariableW },
        { "SetEnvironmentVariableW", (void *)k_SetEnvironmentVariableW },
        { "VirtualAlloc", (void *)k_VirtualAlloc }, { "VirtualFree", (void *)k_VirtualFree },
        { "VirtualProtect", (void *)k_VirtualProtect }, { "VirtualQuery", (void *)k_VirtualQuery },
        { "FlushInstructionCache", (void *)k_FlushInstructionCache }, { "GetCurrentProcess", (void *)k_GetCurrentProcess },
        { "GetLastError", (void *)k_GetLastError }, { "CreateFileW", (void *)k_CreateFileW },
        { "WriteFile", (void *)k_WriteFile }, { "CloseHandle", (void *)k_CloseHandle },
        { "OutputDebugStringW", (void *)k_OutputDebugStringW }, { "LoadLibraryW", (void *)k_LoadLibraryW },
        { "GetProcAddress", (void *)k_GetProcAddress },
    };
    (void)index;
    if (strcasecmp(dll, "kernel32.dll")) { printf("plugin imports from unexpected dll %s\n", dll); exit(2); }
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) if (!strcmp(fn, k[i].name)) return k[i].fn;
    printf("plugin imports unexpected function %s\n", fn); exit(2);
}

/* ------------------------------------------------------------------ */
/* Fake TEB (gs:) so the editor's TLS accesses work                     */
/* ------------------------------------------------------------------ */

static void setup_teb(void)
{
    static u8 teb[0x2000] __attribute__((aligned(16)));
    static u8 peb[0x1000];
    static void *tlsArray[64];
    int local;
    for (int i = 0; i < 64; i++) tlsArray[i] = calloc(1, 0x4000);
    memcpy(tlsArray[0], (void *)0x143524000ULL, 0x6C);    /* editor TLS template (index 0) */
    *(u64 *)(teb + 0x08) = (u64)&local + 0x100000;          /* StackBase  */
    *(u64 *)(teb + 0x10) = 0;                               /* StackLimit */
    *(u64 *)(teb + 0x30) = (u64)teb;
    *(u64 *)(teb + 0x40) = 2;
    *(u64 *)(teb + 0x48) = 1;
    *(u64 *)(teb + 0x58) = (u64)tlsArray;
    *(u64 *)(teb + 0x60) = (u64)peb;
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, teb)) { perror("arch_prctl"); exit(2); }
}

/* ------------------------------------------------------------------ */
/* Crash reporting                                                     */
/* ------------------------------------------------------------------ */

static void on_crash(int sig, siginfo_t *si, void *ctx)
{
    ucontext_t *uc = ctx;
    u64 rip = uc->uc_mcontext.gregs[REG_RIP], rsp = uc->uc_mcontext.gregs[REG_RSP];
    printf("CRASH: signal %d at rip=%#lx (fault addr %p), rsp=%#lx\n", sig, rip, si->si_addr, rsp);
    printf("  possible return addresses on the stack:");
    for (int i = 0; i < 96; i++) {
        u64 v = ((u64 *)rsp)[i];
        if ((v >= EXE_BASE && v < EXE_BASE + g_exeSize) || (v >= (u64)g_plugin && v < (u64)g_plugin + g_pluginSize))
            printf(" %#lx", v);
    }
    printf("\n");
    fflush(stdout);
    _exit(4);
}

/* ------------------------------------------------------------------ */
/* Loading                                                             */
/* ------------------------------------------------------------------ */

typedef MSABI int (*DllMainFn)(void *, u32, void *);
static DllMainFn g_dllMain;

static u8 *load_plugin(const char *path, u32 *size)
{
    size_t fsz;
    u8 *file = read_file(path, &fsz);
    u32 nt = pe_nt(file);
    u32 sizeOfImage = *(u32 *)(file + nt + 24 + 56);
    u64 imageBase = *(u64 *)(file + nt + 24 + 24);
    u8 *base = mmap(0, sizeOfImage, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    map_image(file, base, size);
    apply_relocs2(base, (s64)((u64)base - imageBase));
    resolve_imports(base, plugin_import);
    g_dllMain = (DllMainFn)(base + *(u32 *)(file + nt + 24 + 16));
    free(file);
    return base;
}

static void load_editor(const char *path)
{
    size_t fsz;
    u8 *file = read_file(path, &fsz);
    u32 nt = pe_nt(file);
    u32 sizeOfImage = *(u32 *)(file + nt + 24 + 56);
    void *p = mmap((void *)EXE_BASE, sizeOfImage, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)EXE_BASE) { perror("map editor at 0x140000000"); exit(2); }
    map_image(file, g_exe, &g_exeSize);
    g_thunks = mmap(0, 32 * 1024, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    resolve_imports(g_exe, editor_import);
    free(file);
}

/* ------------------------------------------------------------------ */
/* What the plugin adds                                                */
/* ------------------------------------------------------------------ */

static const struct Cvar { const char *name; u64 addr; const char *type; int saved; const char *min, *max; } k_cvars[] = {
    { "hud_bShowGeometryInfo",         0x142AB9874ULL, "BOOL",  0, 0,   0   },
    { "res_bSaveInTextFormat",         0x141EFB2D8ULL, "INDEX", 1, "0", "2" },
    { "scr_bUnsecureScripting",        0x142A72FD8ULL, "BOOL",  0, 0,   0   },
    { "prj_bUnlockAutoShotgunScope",   0x143279248ULL, "BOOL",  0, 0,   0   },
    { "men_bMenuFrame",                0x143289A70ULL, "BOOL",  0, 0,   0   },
    { "prj_bAllowLevelChangeInEditor", 0x143278EACULL, "BOOL",  1, 0,   0   },
    { "fxd_bShowDebug",                0x142AC5C50ULL, "BOOL",  0, 0,   0   },
    { "fil_bReportFileOpen",           0x1431DE4C8ULL, "BOOL",  0, 0,   0   },
    { "md_bReportReadStats",           0x1431DE960ULL, "BOOL",  1, 0,   0   },
};
#define NCV ((int)(sizeof k_cvars / sizeof k_cvars[0]))

static const struct Site { u64 site, orig; const char *what; } k_sites[] = {
    { 0x140B6A72BULL, 0x140B2E3E0ULL, "RegisterDeclarations in the Core module initializer" },
    { 0x14075B0D6ULL, 0x14075B430ULL, "LoadCvarsHelp in the list of all cvars" },
    { 0x14075B281ULL, 0x14075B430ULL, "LoadCvarsHelp in the \"help\" command" },
    { 0x14075BFCDULL, 0x14075B430ULL, "LoadCvarsHelp in the help lookup (console tooltip)" },
    { 0x140160B48ULL, 0x141611F00ULL, "GetGlobalCommandHandler in the editor startup" },
};
#define NSITES ((int)(sizeof k_sites / sizeof k_sites[0]))

#define NAME_BYTE ((volatile u8 *)0x140B730F3ULL)

/* ------------------------------------------------------------------ */
/* Checks                                                              */
/* ------------------------------------------------------------------ */

static u8 *call_target(u64 site) { u8 *s = (u8 *)site; return s + 5 + *(int32_t *)(s + 1); }
static int in_plugin(u64 a) { return a >= (u64)g_plugin && a < (u64)g_plugin + g_pluginSize; }
static int sites_untouched(void)
{
    for (int i = 0; i < NSITES; i++)
        if (*(u8 *)k_sites[i].site != 0xE8 || call_target(k_sites[i].site) != (u8 *)k_sites[i].orig) return 0;
    return 1;
}

static int log_has(const char *s) { return strstr(g_logText, s) != 0; }
static int log_count(const char *s)
{
    int n = 0;
    for (const char *p = g_logText; (p = strstr(p, s)); p++) n++;
    return n;
}

typedef struct { char *str; } CString;
typedef MSABI void (*fn_void)(void);
typedef MSABI u64 (*fn_u64)(void);
typedef MSABI CString *(*fn_get_record)(CString *out, const char *name);
typedef MSABI CString *(*fn_build_tip)(CString *out, const char *record);
typedef MSABI CString *(*fn_cstr_ctor)(CString *out, const char *s);
typedef MSABI int (*fn_help_cmd)(CString *line);
typedef MSABI void *(*fn_desc_ctor)(void *d);
typedef MSABI void (*fn_desc_expand)(void *arr, int n);
typedef MSABI CString *(*fn_cstr_assign)(CString *s, const char *v);
typedef MSABI const char *(*fn_id_to_str)(u64 id);
typedef MSABI u64 *(*fn_name_id)(u64 *out, const char *name);

static CString cstr(const char *s) { CString c = { 0 }; ((fn_cstr_ctor)0x140B25C50ULL)(&c, s); return c; }

static void print_block(const char *title, const char *text)
{
    printf("  %s:\n", title);
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int n = nl ? (int)(nl - p) : (int)strlen(p);
        printf("    | %.*s\n", n, p);
        if (!nl) break;
        p = nl + 1;
    }
}

static void test_install_ok(void)
{
    g_dllMain(g_plugin, 1, (void *)1);
    CHECK(log_has("] applied to Sam2017_SeriousEditor.exe (5 call redirect(s)"), "plugin reports it was applied (5 redirects)");
    CHECK(g_boxes == 0, "no message box");
    u64 blk = (u64)call_target(k_sites[0].site) & ~0xFFFFULL;
    CHECK(blk < EXE_BASE && EXE_BASE - blk < 0x80000000ULL, "stub block %#lx is just below the editor image", blk);
    CHECK(*(u32 *)blk == 0x43373153 && !strcmp((char *)blk + 8, "MoreConsoleCommandsSED"), "stub block carries the plugin signature");
    u64 helpHook = 0;
    for (int i = 0; i < NSITES; i++) {
        u8 *t = call_target(k_sites[i].site);
        u64 dest = *(u64 *)(t + 6);
        CHECK(*(u8 *)k_sites[i].site == 0xE8 && t != (u8 *)k_sites[i].orig && ((u64)t & ~0xFFFFULL) == blk &&
              t[0] == 0xFF && t[1] == 0x25 && in_plugin(dest),
              "call at %#lx (%s) -> stub %p -> plugin %#lx", k_sites[i].site, k_sites[i].what, t, dest);
        if (i >= 1 && i <= 3) { if (!helpHook) helpHook = dest; else CHECK(dest == helpHook, "  same help hook for all three loader calls"); }
    }
    int rw = 0;
    for (int i = 0; i < g_nprot; i++)
        if (g_prot[i].addr == 0x141EFB2D8ULL && g_prot[i].prot == 0x04) rw = 1;
    CHECK(rw, "page of res_bSaveInTextFormat (0x141EFB2D8, .rdata) made PAGE_READWRITE");
    int touched = 0;
    for (int i = 0; i < g_nprot; i++)
        for (int c = 0; c < NCV; c++)
            if (c != 1 && g_prot[i].addr == k_cvars[c].addr) touched = 1;
    CHECK(!touched, "the eight variables in .data (already writable) left alone");
    CHECK(log_has("res_bSaveInTextFormat (page made writable)"), "log names the variable whose page was made writable");
    CHECK(log_has("and the command \"Toggle metadata codenames\" (Ctrl+Shift+Alt+C)"), "log mentions the command");
    CHECK(g_pinned, "plugin pinned itself in memory");
    CHECK(*NAME_BYTE == 0x18, "metadata name byte untouched at install (0x18)");
    WCHAR v[8];
    CHECK(k_GetEnvironmentVariableW((WCHAR[]){'S','A','M','2','0','1','7','_','M','o','r','e','C','o','n','s','o','l','e',
                                               'C','o','m','m','a','n','d','s','S','E','D',0}, v, 8) == 1,
          "SAM2017_MoreConsoleCommandsSED=1 marks it applied");
}

/* --- registration with a recording RegisterDeclarations -------------------------- */

static int g_regCalls;
static u64 g_regTables[8];
MSABI static void fake_RegisterDeclarations(const u64 *table) { if (g_regCalls < 8) g_regTables[g_regCalls] = (u64)table; g_regCalls++; }

static void dump_table(const u64 *t)
{
    printf("  plugin declaration table at %p:\n", (void *)t);
    int k = 0;
    while (t[k] != ~0ULL && k < 200) {
        printf("    kind %lu  name \"%s\"  user name \"%s\"  var %#lx  typekind %lu  type \"%s\"  flags %#lx  attrs %lu",
               t[k], (char *)t[k + 1], (char *)t[k + 2], t[k + 3], t[k + 4], (char *)t[k + 5], t[k + 6], t[k + 7]);
        for (int a = 0; a < (int)t[k + 7]; a++) printf("  %s=%s", (char *)t[k + 8 + 2 * a], (char *)t[k + 9 + 2 * a]);
        printf("\n");
        k += 8 + 2 * (int)t[k + 7];
    }
    printf("    end marker at index %d\n", k);
}

static void test_registration_with_fake(void)
{
    u8 saved[12];
    memcpy(saved, (void *)0x140B2E3E0ULL, 12);
    write_jmp(0x140B2E3E0ULL, (void *)fake_RegisterDeclarations);
    g_regCalls = 0;
    ((fn_void)0x140B6A720ULL)();                      /* the Core module declaration initializer */
    CHECK(g_regCalls == 2, "RegisterDeclarations called %d times (editor table, then plugin table)", g_regCalls);
    CHECK(g_regTables[0] == 0x141EE3410ULL, "first call still registers the editor's own table (%#lx)", g_regTables[0]);
    if (g_regCalls >= 2) {
        const u64 *t = (const u64 *)g_regTables[1];
        dump_table(t);
        int k = 0;
        for (int c = 0; c < NCV; c++) {
            const struct Cvar *cv = &k_cvars[c];
            int nat = (cv->min ? 1 : 0) + (cv->max ? 1 : 0);
            int ok = t[k] == 2 && !strcmp((char *)t[k + 1], cv->name) && !strcmp((char *)t[k + 2], "") &&
                     t[k + 3] == cv->addr && t[k + 4] == 1 && !strcmp((char *)t[k + 5], cv->type) &&
                     t[k + 6] == (cv->saved ? 0x10006ULL : 0x10002ULL) && t[k + 7] == (u64)nat;
            if (ok && cv->min) ok = !strcmp((char *)t[k + 8], "min") && !strcmp((char *)t[k + 9], cv->min) &&
                                    !strcmp((char *)t[k + 10], "max") && !strcmp((char *)t[k + 11], cv->max);
            CHECK(ok, "record %d: %s -> %#lx, %s, flags %#x%s", c + 1, cv->name, cv->addr, cv->type,
                  cv->saved ? 0x10006 : 0x10002, cv->min ? ", min 0, max 2" : "");
            k += 8 + 2 * nat;
        }
        CHECK(t[k] == ~0ULL, "table terminated with -1 after %d records", NCV);
    }
    g_regCalls = 0;
    ((fn_void)0x140B6A720ULL)();
    CHECK(g_regCalls == 1, "a second run of the initializer does not register the plugin cvars twice");
    memcpy((void *)0x140B2E3E0ULL, saved, 12);
}

/* --- console help ------------------------------------------------------------------ */

typedef struct { u8 *data; int count, alloc, step; } DescArray;
#define HELP_ARRAY ((DescArray *)0x142903E28ULL)
#define HELP_LOADED (*(volatile int *)0x142AE59D0ULL)
enum { F_NAME, F_SOURCE, F_BRIEF, F_DETAIL, F_SAVED, F_PURITY, F_METACONST, F_TYPE, F_FUNCTION, F_PARAMS, F_USERNAME };

static CString *help_record(const char *name)
{
    for (int i = 0; i < HELP_ARRAY->count; i++) {
        CString *d = (CString *)(HELP_ARRAY->data + 0x58 * i);
        if (d[F_NAME].str && !strcasecmp(d[F_NAME].str, name)) return d;
    }
    return 0;
}
static int help_count(const char *name)
{
    int n = 0;
    for (int i = 0; i < HELP_ARRAY->count; i++) {
        CString *d = (CString *)(HELP_ARRAY->data + 0x58 * i);
        if (d[F_NAME].str && !strcasecmp(d[F_NAME].str, name)) n++;
    }
    return n;
}
static void add_file_record(const char *name, const char *brief, const char *detail, const char *saved,
                            const char *purity, const char *type)
{
    DescArray *arr = HELP_ARRAY;
    if (arr->count + 1 > arr->alloc) ((fn_desc_expand)0x14075AF20ULL)(arr, (arr->count / arr->step + 1) * arr->step);
    CString *d = (CString *)(arr->data + 0x58 * arr->count);
    ((fn_desc_ctor)0x14075ACF0ULL)(d);
    arr->count++;
    fn_cstr_assign as = (fn_cstr_assign)0x140B25F30ULL;
    as(&d[F_NAME], name); as(&d[F_SOURCE], "Engine/Somewhere.cpp"); as(&d[F_BRIEF], brief); as(&d[F_DETAIL], detail);
    as(&d[F_SAVED], saved); as(&d[F_PURITY], purity); as(&d[F_METACONST], "false"); as(&d[F_TYPE], type);
    as(&d[F_FUNCTION], "false");
}

/* ListFiles("Help/Cvars/", "*_cvars.xml", ...) inside the loader: no files are listed; in
   the "files" scenario the records the XML parser would make are added right here. */
static int g_listCalls, g_withFiles;
static u64 g_listArgs[4];
MSABI static void fake_ListFiles(u64 dir, u64 pattern, u64 result, u64 flags)
{
    g_listCalls++; g_listArgs[0] = dir; g_listArgs[1] = pattern; g_listArgs[2] = result; g_listArgs[3] = flags;
    if (!g_withFiles) return;
    add_file_record("con_bSomethingElse", "Something unrelated.", "", "true", "pure", "BOOL");
    add_file_record("hud_bShowGeometryInfo", "Show geometry information on the HUD.", "", "true", "pure", "BOOL");
    add_file_record("res_bSaveInTextFormat", "", "", "false", "pure", "BOOL");
    add_file_record("FIL_BREPORTFILEOPEN", "Report every file that is opened.", "", "false", "pure", "BOOL");
    add_file_record("md_bReportReadStats", "", "Report read statistics of metadata.", "TRUE", "pure", "BOOL");
}

static const char *g_expectValues[NCV];

static CString tooltip_of(const char *name)
{
    CString rec = { 0 }, tip = { 0 };
    ((fn_get_record)0x14075C060ULL)(&rec, name);   /* what the console asks for */
    if (!rec.str || !rec.str[0]) return tip;
    ((fn_build_tip)0x14075BBA0ULL)(&tip, rec.str); /* the tooltip / "help" text */
    return tip;
}

static void check_tooltip(const char *name, const char *header, const char *desc, const char *notIn)
{
    CString tip = tooltip_of(name);
    CHECK(tip.str && tip.str[0], "help record found for %s", name);
    if (!tip.str || !tip.str[0]) return;
    print_block("tooltip text built by the editor", tip.str);
    char hdr[256]; snprintf(hdr, sizeof hdr, "%s\n", header);
    char dsc[512]; snprintf(dsc, sizeof dsc, "\n%s\n", desc);
    CHECK(!strncmp(tip.str, hdr, strlen(hdr)), "tooltip of %s starts with \"%s\"", name, header);
    CHECK(strstr(tip.str, dsc) != 0, "tooltip of %s has the description \"%s\"", name, desc);
    if (notIn) CHECK(!strstr(tip.str, notIn), "tooltip of %s does not contain \"%s\"", name, notIn);
    for (int c = 0; c < NCV; c++)
        if (!strcmp(k_cvars[c].name, name) && g_expectValues[c])
            CHECK(strstr(tip.str, g_expectValues[c]) != 0, "tooltip of %s shows \"%.*s\"", name,
                  (int)strlen(g_expectValues[c]) - 1, g_expectValues[c]);
}

static void check_fallback_records(int expectCount)
{
    CHECK(HELP_ARRAY->count == expectCount, "help records in the editor's list: %d (expected %d)", HELP_ARRAY->count, expectCount);
    for (int c = 0; c < NCV; c++) {
        CString *d = help_record(k_cvars[c].name);
        if (!d) { CHECK(0, "record for %s", k_cvars[c].name); continue; }
        printf("    %-30s brief \"%s\" detail \"%s\" saved \"%s\" purity \"%s\" type \"%s\" source \"%s\"\n", d[F_NAME].str,
               d[F_BRIEF].str, d[F_DETAIL].str, d[F_SAVED].str, d[F_PURITY].str, d[F_TYPE].str, d[F_SOURCE].str);
    }
}

static void test_help(void)
{
    u8 saved[12];
    memcpy(saved, (void *)0x140B5FDE0ULL, 12);
    write_jmp(0x140B5FDE0ULL, (void *)fake_ListFiles);

    printf("  (a) no help files: the console tooltip asks for help (lookup -> loader through 0x14075BFCD)\n");
    g_withFiles = 0; g_listCalls = 0;
    int logs0 = log_count("] console help:");
    check_tooltip("res_bSaveInTextFormat", "saved pure INDEX res_bSaveInTextFormat", "(undocumented)", 0);
    CHECK(g_listCalls == 1, "help loader listed the help files once");
    CHECK(!strcmp((char *)g_listArgs[0], "Help/Cvars/") && !strcmp((char *)g_listArgs[1], "*_cvars.xml"),
          "the loader read \"%s\" \"%s\" as usual", (char *)g_listArgs[0], (char *)g_listArgs[1]);
    CHECK(HELP_LOADED == 1, "help loader marked the help as loaded");
    CHECK(log_count("] console help:") == logs0 + 1, "plugin completed the help records once");
    check_fallback_records(NCV);
    int allUndoc = 1;
    for (int c = 0; c < NCV; c++) {
        CString *d = help_record(k_cvars[c].name);
        if (!d || strcmp(d[F_BRIEF].str, "(undocumented)") || d[F_DETAIL].str[0] || strcmp(d[F_TYPE].str, k_cvars[c].type) ||
            strcmp(d[F_SAVED].str, k_cvars[c].saved ? "true" : "false") || strcmp(d[F_PURITY].str, "pure")) allUndoc = 0;
    }
    CHECK(allUndoc, "all nine get the editor-style record: \"(undocumented)\", their type, saved true/false, pure");
    check_tooltip("hud_bShowGeometryInfo", "pure BOOL hud_bShowGeometryInfo", "(undocumented)", "saved");
    check_tooltip("prj_bAllowLevelChangeInEditor", "saved pure BOOL prj_bAllowLevelChangeInEditor", "(undocumented)", 0);
    check_tooltip("md_bReportReadStats", "saved pure BOOL md_bReportReadStats", "(undocumented)", 0);
    check_tooltip("scr_bUnsecureScripting", "pure BOOL scr_bUnsecureScripting", "(undocumented)", "saved");
    CHECK(g_listCalls == 1 && log_count("] console help:") == logs0 + 1, "later lookups do not load or change anything again");
    {
        CString rec = { 0 };
        ((fn_get_record)0x14075C060ULL)(&rec, "hud_bNoSuchThing");
        CHECK(rec.str && !rec.str[0], "unknown names still return no help (empty record)");
    }

    printf("  (b) help files with records for some of them: reload through the list of all cvars (0x14075B0D6)\n");
    HELP_LOADED = 0; g_withFiles = 1; edlog_clear();
    g_quietLog = 1;
    ((fn_void)0x14075B0D0ULL)();
    g_quietLog = 0;
    CHECK(g_listCalls == 2 && HELP_LOADED == 1, "help reloaded");
    CHECK(edlog_has("List of all console variables:") && edlog_has("[4]res_bSaveInTextFormat\n") &&
          edlog_has("[4]con_bSomethingElse\n") && edlog_has("[4]md_bReportReadStats\n"), "the editor's list shows them");
    check_fallback_records(5 + 5);
    CHECK(help_count("fil_bReportFileOpen") == 1, "a record that differs only in letter case is used, not duplicated");
    {
        CString *h = help_record("hud_bShowGeometryInfo"), *r = help_record("res_bSaveInTextFormat");
        CString *f = help_record("fil_bReportFileOpen"), *m = help_record("md_bReportReadStats");
        CString *o = help_record("con_bSomethingElse"), *s = help_record("scr_bUnsecureScripting");
        CHECK(h && !strcmp(h[F_BRIEF].str, "Show geometry information on the HUD.") && !strcmp(h[F_SAVED].str, "false") &&
              !strcmp(h[F_SOURCE].str, "Engine/Somewhere.cpp"),
              "hud_bShowGeometryInfo keeps the description from the files; saved true -> false (not saved here)");
        CHECK(r && !strcmp(r[F_BRIEF].str, "(undocumented)") && !strcmp(r[F_TYPE].str, "INDEX") && !strcmp(r[F_SAVED].str, "true"),
              "res_bSaveInTextFormat had no description: \"(undocumented)\", type BOOL -> INDEX, saved");
        CHECK(f && !strcmp(f[F_BRIEF].str, "Report every file that is opened.") && !strcmp(f[F_SAVED].str, "false"),
              "FIL_BREPORTFILEOPEN record left as it is");
        CHECK(m && !strcmp(m[F_BRIEF].str, "") && !strcmp(m[F_DETAIL].str, "Report read statistics of metadata.") &&
              !strcmp(m[F_SAVED].str, "TRUE"), "md_bReportReadStats: detail kept, no \"(undocumented)\" added, TRUE kept");
        CHECK(o && !strcmp(o[F_BRIEF].str, "Something unrelated.") && !strcmp(o[F_SAVED].str, "true"), "other records untouched");
        CHECK(s && !strcmp(s[F_BRIEF].str, "(undocumented)") && !strcmp(s[F_SOURCE].str, "Sam2017_MoreConsoleCommandsSED.asi"),
              "the cvars missing from the files got the plugin's record");
    }
    CHECK(log_has("console help: hud_bShowGeometryInfo from the help files, shown as not saved; "
                  "res_bSaveInTextFormat from the help files, no description there, shown as (undocumented), type BOOL shown as INDEX, shown as saved; "
                  "scr_bUnsecureScripting not in the help files, shown as (undocumented);"),
          "log line describes where each description came from");
    check_tooltip("hud_bShowGeometryInfo", "pure BOOL hud_bShowGeometryInfo", "Show geometry information on the HUD.", "saved");
    check_tooltip("res_bSaveInTextFormat", "saved pure INDEX res_bSaveInTextFormat", "(undocumented)", 0);
    check_tooltip("md_bReportReadStats", "saved pure BOOL md_bReportReadStats", "Report read statistics of metadata.", "(undocumented)");

    printf("  (c) reload without files through the \"help\" command (0x14075B281), then \"help <name>\"\n");
    HELP_LOADED = 0; g_withFiles = 0; edlog_clear();
    {
        CString line = cstr("help");
        g_quietLog = 1;
        ((fn_help_cmd)0x14075B260ULL)(&line);
        g_quietLog = 0;
    }
    CHECK(g_listCalls == 3 && HELP_LOADED == 1, "help reloaded");
    check_fallback_records(NCV);
    edlog_clear();
    {
        CString line = cstr("help prj_bUnlockAutoShotgunScope");
        ((fn_help_cmd)0x14075B260ULL)(&line);
    }
    CHECK(edlog_has("pure BOOL prj_bUnlockAutoShotgunScope\n") && edlog_has("\n(undocumented)\n"),
          "\"help prj_bUnlockAutoShotgunScope\" prints the help");
    memcpy((void *)0x140B5FDE0ULL, saved, 12);
}

/* --- the command, with recording RegisterCommand / RegisterShortcut ----------------- */

static struct { int n; u64 handler; char id[128], tip[128]; u64 exec, update, extra; } g_cmd;
static struct { int n; u64 handler; char id[128], sid[128]; u32 key, event; u64 extra; } g_sc;
MSABI static void fake_RegisterCommand(u64 h, const char *id, const char *tip, u64 exec, u64 update, u64 extra)
{
    g_cmd.n++; g_cmd.handler = h; snprintf(g_cmd.id, 128, "%s", id); snprintf(g_cmd.tip, 128, "%s", tip);
    g_cmd.exec = exec; g_cmd.update = update; g_cmd.extra = extra;
}
MSABI static void fake_RegisterShortcut(u64 h, const char *id, const char *sid, u32 key, u32 event, u64 extra)
{
    g_sc.n++; g_sc.handler = h; snprintf(g_sc.id, 128, "%s", id); snprintf(g_sc.sid, 128, "%s", sid);
    g_sc.key = key; g_sc.event = event; g_sc.extra = extra;
}
typedef MSABI int (*fn_cmd)(void *);

static void test_toggle(u64 exec)
{
    int np = g_nprot, nf = g_flushes;
    u8 before = *NAME_BYTE;
    edlog_clear();
    int r = ((fn_cmd)exec)(0);
    CHECK(r == 1 && *NAME_BYTE == (before == 0x18 ? 0x20 : 0x18), "command returns 1 (handled), byte at 0x140B730F3: %#x -> %#x",
          before, *NAME_BYTE);
    CHECK(g_nprot == np + 2 && g_prot[np].addr == 0x140B730F3ULL && g_prot[np].size == 1 && g_prot[np].prot == 0x40 &&
          g_prot[np + 1].addr == 0x140B730F3ULL && g_prot[np + 1].prot == 0x20 && g_flushes == nf + 1,
          "made writable for the write, protection restored, instruction cache flushed");
    CHECK(edlog_has(*NAME_BYTE == 0x20 ? "[4]Metadata codenames ON: metadata members are shown by their code names.\n"
                                       : "[4]Metadata codenames OFF: metadata members are shown by their display names.\n"),
          "console message (Info) says %s", *NAME_BYTE == 0x20 ? "ON" : "OFF");
    u8 code[9]; memcpy(code, (void *)0x140B730F0ULL, 9);
    CHECK(code[0] == 0x48 && code[1] == 0x8B && code[2] == 0x49 && code[4] == 0xE9 && *(u32 *)(code + 5) == 0xFFFB78F7,
          "the rest of the getter is unchanged (48 8B 49 %02X E9 F7 78 FB FF)", code[3]);
}

static void test_command_fake(void)
{
    u8 s1[12], s2[12];
    memcpy(s1, (void *)0x141610600ULL, 12); memcpy(s2, (void *)0x141610430ULL, 12);
    write_jmp(0x141610600ULL, (void *)fake_RegisterCommand);
    write_jmp(0x141610430ULL, (void *)fake_RegisterShortcut);
    *(u64 *)0x1432AAFA8ULL = 0x5150;                         /* CommandHandler.Global */
    u64 h = ((fn_u64)call_target(0x140160B48ULL))();         /* what the editor startup calls */
    CHECK(h == 0x5150, "the editor still gets CommandHandler.Global (%#lx)", h);
    CHECK(g_cmd.n == 1 && g_cmd.handler == 0x5150 && !strcmp(g_cmd.id, "Toggle metadata codenames") &&
          !strcmp(g_cmd.tip, "Toggle metadata codenames") && in_plugin(g_cmd.exec) && g_cmd.update == 0 && g_cmd.extra == 0,
          "RegisterCommand(Global, \"%s\", \"%s\", plugin %#lx, 0, 0)", g_cmd.id, g_cmd.tip, g_cmd.exec);
    CHECK(g_sc.n == 1 && g_sc.handler == 0x5150 && !strcmp(g_sc.id, "Toggle metadata codenames") && !strcmp(g_sc.sid, "") &&
          g_sc.key == 0xE0000042 && g_sc.event == 2 && g_sc.extra == 0,
          "RegisterShortcut(Global, \"%s\", \"\", %#x = Ctrl+Shift+Alt+C, event %u = key press, 0)", g_sc.id, g_sc.key, g_sc.event);
    CHECK(log_has("command \"Toggle metadata codenames\" registered in CommandHandler.Global with the shortcut Ctrl+Shift+Alt+C "
                  "(metadata names currently: display names)"), "registration logged");
    h = ((fn_u64)call_target(0x140160B48ULL))();
    CHECK(h == 0x5150 && g_cmd.n == 1 && g_sc.n == 1, "a second call registers nothing more");
    test_toggle(g_cmd.exec);
    test_toggle(g_cmd.exec);
    *NAME_BYTE = 0x33; edlog_clear();
    ((fn_cmd)g_cmd.exec)(0);
    CHECK(*NAME_BYTE == 0x33 && edlog_has("nothing toggled"), "a byte that is neither 0x18 nor 0x20 is left alone");
    *NAME_BYTE = 0x18;
    *(u64 *)0x1432AAFA8ULL = 0;
    memcpy((void *)0x141610600ULL, s1, 12); memcpy((void *)0x141610430ULL, s2, 12);
}

/* --- the command with the editor's real handler and registration --------------------- */

static void test_command_real(void)
{
    printf("[2] the editor creates CommandHandler.Global (service start 0x141674110)\n");
    ((fn_void)0x141674110ULL)();
    u64 handler = *(u64 *)0x1432AAFA8ULL;
    CHECK(handler != 0, "CommandHandler.Global created at %#lx", handler);
    printf("[3] the editor startup fetches it (0x140160B48) -> plugin registers its command with the real functions\n");
    u64 h = ((fn_u64)call_target(0x140160B48ULL))();
    CHECK(h == handler, "the editor gets the same handler back");
    u64 id = 0; ((fn_name_id)0x140B2AA10ULL)(&id, "Toggle metadata codenames");
    struct { u8 *data; int count, alloc, step; } *cmds = (void *)(handler + 8);
    u64 exec = 0;
    for (int i = 0; i < cmds->count; i++) {
        u64 *e = (u64 *)(cmds->data + 0x28 * i);
        printf("    command %d: \"%s\" tooltip \"%s\" execute %#lx update %#lx\n", i, ((fn_id_to_str)0x140B2A9F0ULL)(e[0]),
               ((fn_id_to_str)0x140B2A9F0ULL)(e[1]), e[3], e[4]);
        if (e[0] == id) exec = e[3];
    }
    CHECK(cmds->count == 1 && exec && in_plugin(exec), "the handler holds the command \"Toggle metadata codenames\" -> plugin");
    struct { u8 *data; int count, alloc; } *shortcuts = (void *)0x1429B05E8ULL;
    int found = 0;
    for (int i = 0; i < shortcuts->count; i++) {
        u32 key = *(u32 *)(shortcuts->data + 16 * i); u64 sid = *(u64 *)(shortcuts->data + 16 * i + 8);
        printf("    shortcut %d: key %#x id \"%s\"\n", i, key, ((fn_id_to_str)0x140B2A9F0ULL)(sid));
        if (key == 0xE0000042 && strstr(((fn_id_to_str)0x140B2A9F0ULL)(sid), "Toggle metadata codenames")) found = 1;
    }
    CHECK(found, "the editor's shortcut table maps Ctrl+Shift+Alt+C (0xE0000042) to it");
    CHECK(log_has("command \"Toggle metadata codenames\" registered in CommandHandler.Global"), "registration logged");
    if (exec) { test_toggle(exec); test_toggle(exec); }

    printf("[4] key presses dispatched by the editor's own shortcut handling (0x14160FF20: handler, context, key, event)\n");
    typedef MSABI int (*fn_shortcut)(u64 handler, u64 ctx, u32 key, u32 event);
    static const struct { u32 key, event; int fires; const char *what; } keys[] = {
        { 0xE0000042, 2, 1, "Ctrl+Shift+Alt+C pressed" },
        { 0xE0000042, 2, 1, "Ctrl+Shift+Alt+C pressed again" },
        { 0xA0000042, 2, 0, "Ctrl+Shift+C pressed" },
        { 0xC0000042, 2, 0, "Ctrl+Alt+C pressed" },
        { 0x00000042, 2, 0, "C pressed" },
        { 0xE0000042, 3, 0, "Ctrl+Shift+Alt+C, another activation event (3)" },
    };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        u8 before = *NAME_BYTE;
        int handled = ((fn_shortcut)0x14160FF20ULL)(handler, 0, keys[i].key, keys[i].event);
        int toggled = *NAME_BYTE != before;
        CHECK(!!handled == keys[i].fires && toggled == keys[i].fires, "%s (%#x, event %u): %s", keys[i].what, keys[i].key,
              keys[i].event, toggled ? "toggled" : "nothing happens");
    }
    CHECK(*NAME_BYTE == 0x18, "back to display names after two toggles");

    printf("[5] running it by name (0x14160FE50: handler, context, command id, ...)\n");
    typedef MSABI int (*fn_execute)(u64 handler, u64 ctx, u64 cmdId, int *index, u64 args);
    int handled = ((fn_execute)0x14160FE50ULL)(handler, 0, id, 0, 0);
    CHECK(handled == 1 && *NAME_BYTE == 0x20, "executed through the handler: code names");
    ((fn_execute)0x14160FE50ULL)(handler, 0, id, 0, 0);
    CHECK(*NAME_BYTE == 0x18, "and back");
}

/* --- real Core module: values, console writes, SeriousEditor.ini ---------------------- */

typedef MSABI int (*fn_get_values)(const char *name, CString *cur, CString *def);
typedef MSABI void (*fn_assign)(u64 *nameId, const CString *value, int version);
typedef MSABI void (*fn_init_module)(const char *name);

static void show_values(const char *name, const char *expCur, const char *expDef)
{
    CString cur = { 0 }, def = { 0 };
    int ok = ((fn_get_values)0x140B33160ULL)(name, &cur, &def);
    CHECK(ok && !strcmp(cur.str, expCur) && !strcmp(def.str, expDef),
          "%s is a declared cvar: current %s, default %s", name, ok ? cur.str : "?", ok ? def.str : "?");
}

static void assign(const char *name, const char *value)
{
    u64 id = 0;
    CString v = { (char *)value };
    ((fn_name_id)0x140B2AA10ULL)(&id, name);
    ((fn_assign)0x140B56420ULL)(&id, &v, -1);
}

static u64 *find_symbol(const char *name)
{
    struct { u64 **data; int count, alloc; } *list = *(void **)0x1431E4800ULL;
    for (int i = 0; list && i < list->count; i++)
        if (!strcmp(((fn_id_to_str)0x140B2A9F0ULL)(list->data[i][0]), name)) return list->data[i];
    return 0;
}

/* Lua stand-ins for the console's "name = value": only the two stack accessors are faked,
   the editor's own lua_type / lua_tointeger / lua_toboolean then read this value. */
static struct { double n; int tt; int pad; } g_tv;
static const char *g_luaKey;
MSABI static void *fake_index2addr(void *L, int idx) { (void)L; (void)idx; return &g_tv; }
MSABI static const char *fake_tolstring(void *L, int idx, size_t *len)
{
    (void)L; (void)idx;
    if (len) *len = strlen(g_luaKey);
    return g_luaKey;
}
typedef MSABI int (*fn_newindex)(void *L);
static void console_set(const char *name, double value)
{
    g_luaKey = name; g_tv.n = value; g_tv.tt = 3;           /* LUA_TNUMBER */
    ((fn_newindex)0x1407A7650ULL)((void *)0x1000);
}

/* The editor's file stream, faked for SeriousEditor.ini */
static char g_iniPath[512], g_iniMode[16], g_iniOut[1 << 16];
static size_t g_iniOutLen;
static const char *const *g_iniIn;
static int g_iniInPos;
MSABI static void *fs_ctor(u64 *s) { s[0] = 0x51; s[1] = 0; return s; }
MSABI static void fs_open(u64 *s, u64 *err, const char *path, const char *mode, const char *x)
{
    (void)s; (void)x; *err = 0; g_iniInPos = 0;
    snprintf(g_iniPath, sizeof g_iniPath, "%s", path); snprintf(g_iniMode, sizeof g_iniMode, "%s", mode);
}
MSABI static void fs_printf(u64 *s, u64 *err, const char *fmt, u32 tag, const char *arg)
{
    (void)s; (void)fmt; (void)tag; *err = 0;
    size_t n = strlen(arg);
    if (g_iniOutLen + n + 1 < sizeof g_iniOut) { memcpy(g_iniOut + g_iniOutLen, arg, n + 1); g_iniOutLen += n; }
}
MSABI static void fs_close(u64 *s) { (void)s; }
MSABI static void fs_prepare(u64 *s, u64 *err) { (void)s; *err = 0; }
MSABI static int fs_eof(u64 *s) { (void)s; return !g_iniIn || !g_iniIn[g_iniInPos]; }
MSABI static void fs_getline(u64 *s, CString *out, u64 *err)
{
    (void)s; *err = 0;
    ((fn_cstr_ctor)0x140B25C50ULL)(out, g_iniIn[g_iniInPos++]);
}
MSABI static int fake_FileExists(const char *path) { (void)path; return 1; }

typedef MSABI void (*fn_save)(CString *path, int savedOnly);
typedef MSABI int (*fn_load)(CString *path);

static void ini_line_check(const char *line, int expect)
{
    char l[256]; snprintf(l, sizeof l, "%s\n", line);
    int has = strstr(g_iniOut, l) != 0;
    CHECK(has == expect, "SeriousEditor.ini %s \"%s\"", expect ? "has" : "has no line for", line);
}

static void test_ini(void)
{
    printf("[5] closing the editor: its own cvar saving (0x140B572E0, saved cvars only) with a faked file stream\n");
    write_jmp(0x140B5A240ULL, (void *)fs_ctor);
    write_jmp(0x140B5B6A0ULL, (void *)fs_open);
    write_jmp(0x140B5ADB0ULL, (void *)fs_printf);
    write_jmp(0x140B5A2A0ULL, (void *)fs_close);
    write_jmp(0x140B5BA80ULL, (void *)fs_prepare);
    write_jmp(0x140B5B390ULL, (void *)fs_eof);
    write_jmp(0x140B5B070ULL, (void *)fs_getline);
    write_jmp(0x140B63C50ULL, (void *)fake_FileExists);
    const char *ini = "C:\\Games\\Serious Sam Fusion 2017\\Bin\\x64\\SeriousEditor.ini";
    assign("res_bSaveInTextFormat", "2");
    assign("prj_bAllowLevelChangeInEditor", "1");
    assign("md_bReportReadStats", "0");
    assign("hud_bShowGeometryInfo", "1");
    assign("fxd_bShowDebug", "1");
    {
        CString path = cstr(ini);
        g_iniOutLen = 0; g_iniOut[0] = 0;
        ((fn_save)0x140B572E0ULL)(&path, 1);
    }
    CHECK(!strcmp(g_iniPath, ini), "written to %s (mode \"%s\")", g_iniPath, g_iniMode);
    print_block("SeriousEditor.ini as the editor writes it", g_iniOut);
    ini_line_check("res_bSaveInTextFormat = 2;", 1);
    ini_line_check("prj_bAllowLevelChangeInEditor = 1;", 1);
    ini_line_check("md_bReportReadStats = 0;", 1);
    ini_line_check("hud_bShowGeometryInfo = 1;", 0);
    ini_line_check("fxd_bShowDebug = 1;", 0);
    CHECK(!strstr(g_iniOut, "scr_bUnsecureScripting") && !strstr(g_iniOut, "men_bMenuFrame") &&
          !strstr(g_iniOut, "fil_bReportFileOpen") && !strstr(g_iniOut, "prj_bUnlockAutoShotgunScope"),
          "the six cvars that are not saved are not written");

    printf("[6] starting the editor again: its own cvar loading (0x140B577F0) from that file\n");
    assign("res_bSaveInTextFormat", "0");
    assign("prj_bAllowLevelChangeInEditor", "0");
    assign("md_bReportReadStats", "0");
    assign("hud_bShowGeometryInfo", "0");
    assign("fxd_bShowDebug", "0");
    static const char *const lines[] = {
        "res_bSaveInTextFormat = 2;", "prj_bAllowLevelChangeInEditor = 1;", "md_bReportReadStats = 1;",
        "xyz_bNotDeclaredAnywhere = 1;", 0,
    };
    g_iniIn = lines;
    if (!*(u64 *)0x1431DD850ULL) *(u64 *)0x1431DD850ULL = (u64)h_strdup("");   /* global CString (CRT-constructed) */
    {
        CString path = cstr(ini);
        ((fn_load)0x140B577F0ULL)(&path);
    }
    CHECK(*(volatile int *)0x141EFB2D8ULL == 2, "res_bSaveInTextFormat restored to 2");
    CHECK(*(volatile int *)0x143278EACULL == 1, "prj_bAllowLevelChangeInEditor restored to 1");
    CHECK(*(volatile int *)0x1431DE960ULL == 1, "md_bReportReadStats restored to 1");
    show_values("res_bSaveInTextFormat", "2", "0");
    show_values("md_bReportReadStats", "1", "0");
    printf("[7] closing again: values written back, the undeclared line kept as the editor does\n");
    {
        CString path = cstr(ini);
        g_iniOutLen = 0; g_iniOut[0] = 0; g_iniIn = 0;
        ((fn_save)0x140B572E0ULL)(&path, 1);
    }
    print_block("SeriousEditor.ini", g_iniOut);
    ini_line_check("res_bSaveInTextFormat = 2;", 1);
    ini_line_check("prj_bAllowLevelChangeInEditor = 1;", 1);
    ini_line_check("md_bReportReadStats = 1;", 1);
    ini_line_check("xyz_bNotDeclaredAnywhere = 1;", 1);
}

static void test_real_core(void)
{
    printf("[2] registering the editor's basic data types (0x140B417E0), then running its real Core module initialization (0x140B589C0)\n");
    g_quietLog = 1;
    ((fn_void)0x140B417E0ULL)();
    ((fn_init_module)0x140B589C0ULL)((const char *)0x141C56C98ULL);
    g_quietLog = 0;
    printf("  Core module initialized\n");
    CHECK(log_has("console variables declared: BOOL hud_bShowGeometryInfo at 0x142ab9874, saved INDEX res_bSaveInTextFormat (0..2) "
                  "at 0x141efb2d8, BOOL scr_bUnsecureScripting at 0x142a72fd8, BOOL prj_bUnlockAutoShotgunScope at 0x143279248, "
                  "BOOL men_bMenuFrame at 0x143289a70, saved BOOL prj_bAllowLevelChangeInEditor at 0x143278eac, "
                  "BOOL fxd_bShowDebug at 0x142ac5c50, BOOL fil_bReportFileOpen at 0x1431de4c8, saved BOOL md_bReportReadStats "
                  "at 0x1431de960"),
          "the plugin's hook ran inside the Core module initialization");
    u64 boolType = *(u64 *)0x1431DD4F8ULL, indexType = *(u64 *)0x1431DD4E0ULL;
    for (int c = 0; c < NCV; c++) {
        u64 *sym = find_symbol(k_cvars[c].name);
        u32 flags = sym ? *(u32 *)((u8 *)sym + 0x38) : 0;
        int nat = sym ? *(int *)((u8 *)sym + 0x48) : -1;
        CHECK(sym && sym[2] == k_cvars[c].addr && sym[4] == (strcmp(k_cvars[c].type, "BOOL") ? indexType : boolType) &&
              flags == (k_cvars[c].saved ? 0x10006u : 0x10002u) && nat == (k_cvars[c].min ? 2 : 0),
              "symbol %s: var %#lx, %s, flags %#x, %d attribute(s)", k_cvars[c].name, sym ? sym[2] : 0, k_cvars[c].type, flags, nat);
    }
    {
        u64 *sym = find_symbol("res_bSaveInTextFormat");
        CString *attrs = sym ? *(CString **)((u8 *)sym + 0x50) : 0;
        CHECK(attrs && !strcmp(attrs[0].str, "min") && !strcmp(attrs[1].str, "0") && !strcmp(attrs[2].str, "max") &&
              !strcmp(attrs[3].str, "2"), "res_bSaveInTextFormat carries min=0, max=2");
    }
    show_values("md_bLoadIncompatibleTypes", "0", "0");
    for (int c = 0; c < NCV; c++) show_values(k_cvars[c].name, "0", "0");

    printf("[3] assigning values through the editor's cvar assignment (0x140B56420)\n");
    for (int c = 0; c < NCV; c++) {
        assign(k_cvars[c].name, "1");
        int v1 = *(volatile int *)k_cvars[c].addr;
        assign(k_cvars[c].name, "0");
        int v0 = *(volatile int *)k_cvars[c].addr;
        CHECK(v1 == 1 && v0 == 0, "%s = 1 wrote 1 to %#lx, = 0 wrote 0", k_cvars[c].name, k_cvars[c].addr);
    }
    {
        u32 before = *(u32 *)(k_cvars[2].addr + 4), after;
        assign("scr_bUnsecureScripting", "1");
        after = *(u32 *)(k_cvars[2].addr + 4);
        CHECK(before == after, "4-byte BOOL: the next variable is not touched");
        assign("scr_bUnsecureScripting", "0");
    }

    printf("[3b] typing \"name = value\" in the console: the editor's script write handler (0x1407A7650)\n");
    write_jmp(0x140546DB0ULL, (void *)fake_index2addr);   /* Lua stack slot -1: the value  */
    write_jmp(0x140548270ULL, (void *)fake_tolstring);    /* Lua stack slot -2: the name   */
    static const struct { const char *name; double value; int expect; u64 addr; } cases[] = {
        { "res_bSaveInTextFormat", 1, 1, 0x141EFB2D8ULL }, { "res_bSaveInTextFormat", 2, 2, 0x141EFB2D8ULL },
        { "res_bSaveInTextFormat", 5, 2, 0x141EFB2D8ULL }, { "res_bSaveInTextFormat", -3, 0, 0x141EFB2D8ULL },
        { "hud_bShowGeometryInfo", 1, 1, 0x142AB9874ULL }, { "hud_bShowGeometryInfo", 2, 1, 0x142AB9874ULL },
        { "hud_bShowGeometryInfo", 0, 0, 0x142AB9874ULL }, { "scr_bUnsecureScripting", 1, 1, 0x142A72FD8ULL },
        { "scr_bUnsecureScripting", 0, 0, 0x142A72FD8ULL }, { "men_bMenuFrame", 1, 1, 0x143289A70ULL },
        { "men_bMenuFrame", 0, 0, 0x143289A70ULL }, { "md_bReportReadStats", 1, 1, 0x1431DE960ULL },
        { "md_bReportReadStats", 0, 0, 0x1431DE960ULL }, { "res_bSaveInTextFormat", 2, 2, 0x141EFB2D8ULL },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        console_set(cases[i].name, cases[i].value);
        int got = *(volatile int *)cases[i].addr;
        CHECK(got == cases[i].expect, "%s = %g  ->  %d", cases[i].name, cases[i].value, got);
    }
    show_values("res_bSaveInTextFormat", "2", "0");
    for (int c = 0; c < NCV; c++) g_expectValues[c] = "Current value: 0  Default value: 0\n";
    g_expectValues[1] = "Current value: 2  Default value: 0\n";
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *exePath = argc > 1 ? argv[1] : "Sam2017_SeriousEditor.exe";
    const char *pluginPath = argc > 2 ? argv[2] : "Sam2017_MoreConsoleCommandsSED.asi";
    if (argc > 3) g_mode = argv[3];
    setvbuf(stdout, 0, _IONBF, 0);

    struct sigaction sa = { 0 };
    sa.sa_sigaction = on_crash; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGILL, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGFPE, &sa, 0);

    load_editor(exePath);
    setup_teb();
    install_memory_standins();
    utf8_to_w("C:\\Games\\Serious Sam Fusion 2017\\Bin\\x64\\Sam2017_SeriousEditor.exe", g_exePathW, 512);
    utf8_to_w("C:\\Games\\Serious Sam Fusion 2017\\Bin\\x64\\plugins\\Sam2017_MoreConsoleCommandsSED.asi", g_selfPathW, 512);
    g_plugin = load_plugin(pluginPath, &g_pluginSize);
    printf("editor mapped at %p, plugin at %p\n", (void *)g_exe, (void *)g_plugin);

    if (!strcmp(g_mode, "full")) {
        env_set("SAM2017_ASI_WINDOW", "1");
        printf("[1] install (loaded by msimg32.dll before the program starts)\n");
        test_install_ok();
        env_set("SAM2017_ASI_WINDOW", 0);
        printf("[2] Core module initializer -> RegisterDeclarations\n");
        test_registration_with_fake();
        printf("[3] console help\n");
        test_help();
        printf("[4] editor startup -> command and shortcut (recording fakes), then running the command\n");
        test_command_fake();
    } else if (!strcmp(g_mode, "realcore")) {
        env_set("SAM2017_ASI_WINDOW", "1");
        printf("[1] install (loaded by msimg32.dll before the program starts)\n");
        test_install_ok();
        env_set("SAM2017_ASI_WINDOW", 0);
        test_real_core();
        printf("[4] console help -> tooltip text (with values)\n");
        test_help();
        test_ini();
    } else if (!strcmp(g_mode, "realcmd")) {
        env_set("SAM2017_ASI_WINDOW", "1");
        printf("[1] install (loaded by msimg32.dll before the program starts)\n");
        test_install_ok();
        env_set("SAM2017_ASI_WINDOW", 0);
        test_command_real();
    } else if (!strcmp(g_mode, "pretoggled")) {
        printf("[*] an exe whose byte at 0x140B730F3 was already set to 0x20 (code names)\n");
        *NAME_BYTE = 0x20;
        env_set("SAM2017_ASI_WINDOW", "1");
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("] applied to Sam2017_SeriousEditor.exe"), "applied");
        CHECK(*NAME_BYTE == 0x20, "byte left as it was");
        write_jmp(0x141610600ULL, (void *)fake_RegisterCommand);
        write_jmp(0x141610430ULL, (void *)fake_RegisterShortcut);
        *(u64 *)0x1432AAFA8ULL = 0x5150;
        ((fn_u64)call_target(0x140160B48ULL))();
        CHECK(log_has("(metadata names currently: code names)"), "log says code names are shown");
        test_toggle(g_cmd.exec);
        CHECK(*NAME_BYTE == 0x18, "first toggle goes back to display names");
    } else if (!strcmp(g_mode, "badgetter")) {
        printf("[*] the metadata name getter at 0x140B730F0 was changed by something else\n");
        *NAME_BYTE = 0x28;
        env_set("SAM2017_ASI_WINDOW", "1");
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("NOT APPLIED: The program code it relies on is different from what it expects (at 0x140b730f0)"),
              "reported with its address");
        CHECK(g_boxes == 1, "user told with a message box");
        CHECK(sites_untouched() && g_nprot == 0, "editor code untouched, no page protection changed");
    } else if (!strcmp(g_mode, "cookie")) {
        printf("[*] install by another early loader (no SAM2017_ASI_WINDOW, /GS cookie still default)\n");
        test_install_ok();
    } else if (!strcmp(g_mode, "late")) {
        printf("[*] loaded after the program started\n");
        *(u64 *)0x142A32688ULL = 0x1234567890ULL;      /* CRT already initialized the cookie */
        g_dllMain(g_plugin, 1, 0);
        CHECK(log_has("NOT APPLIED: It was loaded after the program had started"), "logged as not applied (late)");
        CHECK(g_boxes == 1 && strstr(g_lastBox, "Sam2017_MoreConsoleCommandsSED.asi was not applied to Sam2017_SeriousEditor.exe."),
              "user told once with a message box");
        CHECK(sites_untouched(), "editor code untouched");
        CHECK(g_nprot == 0, "no page protection changed");
    } else if (!strcmp(g_mode, "twice")) {
        printf("[*] two copies of the plugin (e.g. in scripts\\ and plugins\\)\n");
        env_set("SAM2017_ASI_WINDOW", "1");
        test_install_ok();
        u8 *t0 = call_target(0x140B6A72BULL);
        u32 sz2; u8 *second = load_plugin(pluginPath, &sz2);
        g_logLen = 0; g_logText[0] = 0;
        g_dllMain(second, 1, (void *)1);
        CHECK(log_has("already applied by another copy of this plugin, skipped"), "second copy skips");
        CHECK(call_target(0x140B6A72BULL) == t0, "first copy's redirect kept");
        env_set("SAM2017_MoreConsoleCommandsSED", 0);       /* even without the marker variable */
        g_logLen = 0; g_logText[0] = 0;
        u8 *third = load_plugin(pluginPath, &sz2);
        g_dllMain(third, 1, (void *)1);
        CHECK(log_has("already applied by another copy of this plugin, skipped"), "third copy recognizes the stub signature");
        CHECK(g_boxes == 0, "no message box");
    } else if (!strcmp(g_mode, "conflict")) {
        printf("[*] another mod already changed one of the calls\n");
        env_set("SAM2017_ASI_WINDOW", "1");
        *(int32_t *)0x140160B49ULL += 0x10;
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("NOT APPLIED: The program code it changes was already changed by something else (at 0x140160b48)"),
              "conflict reported with its address");
        CHECK(g_boxes == 1, "user told with a message box");
        *(int32_t *)0x140160B49ULL -= 0x10;
        CHECK(sites_untouched(), "nothing else patched");
        CHECK(g_nprot == 0, "no page protection changed");
    } else if (!strcmp(g_mode, "relied")) {
        printf("[*] code the plugin relies on was changed\n");
        env_set("SAM2017_ASI_WINDOW", "1");
        *(u8 *)0x14075BFF0ULL ^= 1;                    /* inside the help lookup */
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("NOT APPLIED: The program code it relies on is different from what it expects (at 0x14075bfd2)"),
              "reported with the region address");
        CHECK(sites_untouched(), "editor code untouched");
        CHECK(g_nprot == 0, "no page protection changed");
    } else if (!strcmp(g_mode, "game")) {
        printf("[*] loaded by the game, Sam2017_Unrestricted.exe\n");
        utf8_to_w("C:\\Games\\Serious Sam Fusion 2017\\Bin\\x64\\Sam2017_Unrestricted.exe", g_exePathW, 512);
        *(u32 *)(g_exe + pe_nt(g_exe) + 8) = 0x5CF9A1F6;
        *(u32 *)(g_exe + pe_nt(g_exe) + 24 + 56) = 0x027FB000;
        env_set("SAM2017_ASI_WINDOW", "1");
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("nothing to do: Sam2017_Unrestricted.exe (TimeDateStamp 0x5cf9a1f6, SizeOfImage 0x27fb000) is not a program this plugin knows"),
              "logs 'nothing to do' for the game");
        CHECK(g_boxes == 0, "no message box");
    } else if (!strcmp(g_mode, "wrongbuild")) {
        printf("[*] a different build of Sam2017_SeriousEditor.exe\n");
        *(u32 *)(g_exe + pe_nt(g_exe) + 8) = 0x5D000000;
        env_set("SAM2017_ASI_WINDOW", "1");
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("is not the original executable of 7 June 2019"), "logged");
        CHECK(g_boxes == 1 && strstr(g_lastBox, "This Sam2017_SeriousEditor.exe is not the original executable of 7 June 2019"),
              "warning box shown");
        u32 sz2; u8 *second = load_plugin(pluginPath, &sz2);
        g_dllMain(second, 1, (void *)1);
        CHECK(g_boxes == 1, "only one warning per process (SAM2017_ASI_WARNED)");
        CHECK(sites_untouched(), "editor code untouched");
    } else {
        printf("unknown mode %s\n", g_mode);
        return 2;
    }
    printf("%s: %d failure(s)\n", g_mode, g_failures);
    return g_failures ? 1 : 0;
}
