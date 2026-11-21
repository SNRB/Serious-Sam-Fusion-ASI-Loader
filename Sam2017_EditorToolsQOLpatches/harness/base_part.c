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
#include <setjmp.h>

/* while the editor's static initializers run, a crash or an unemulated import skips the one that
   caused it instead of ending the harness */
static sigjmp_buf g_initJmp;
static volatile int g_inInit;
static int g_initSkipped;

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
    if (g_inInit) { g_inInit = 0; siglongjmp(g_initJmp, 2); }
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

MSABI static u64 e_Ret0(void) { return 0; }
MSABI static u64 e_Ident(u64 x) { return x; }
MSABI static u64 e_Ret1(void) { return 1; }

static void *editor_import(const char *dll, const char *fn, int index)
{
    static const char *const ret0[] = { "SetUnhandledExceptionFilter", 0 };
    static const char *const ret1[] = { 0 };
    static const char *const ident1[] = { "EncodePointer", "DecodePointer", 0 };
    for (int i = 0; ident1[i]; i++) if (!strcmp(fn, ident1[i])) return (void *)e_Ident;
    for (int i = 0; ret0[i]; i++) if (!strcmp(fn, ret0[i])) return (void *)e_Ret0;
    for (int i = 0; ret1[i]; i++) if (!strcmp(fn, ret1[i])) return (void *)e_Ret1;
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

/* the memory manager's state block (0x1431DD258): the object ID table (0x140B2C220) takes its lock;
   EnterCriticalSection is a no-op here, so a dummy lock is enough */
MSABI static void *m_state(void)
{
    static u8 dummyLock[0x40];
    *(u64 *)0x1431DD260ULL = (u64)dummyLock;
    return (void *)0x1431DD258ULL;
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

/* "Code guard was activated" (0x140B338B0): reported with its caller, then the editor goes on */
static int g_codeGuards;
MSABI static void code_guard(const char *a, const char *b, const char *c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    g_codeGuards++;
    printf("  CODE GUARD from %p\n", __builtin_return_address(0));
}

static void install_memory_standins(void)
{
    write_jmp(0x140B338B0, (void *)code_guard);
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
    write_jmp(0x140B20A00, (void *)m_state);         /* allocator state block (its lock) */
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
MSABI static u32 k_GetCurrentThreadId(void) { return 1; }
MSABI static void k_Sleep(u32 ms) { (void)ms; }
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
        { "GetCurrentThreadId", (void *)k_GetCurrentThreadId }, { "Sleep", (void *)k_Sleep },
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
    if (g_inInit) { g_inInit = 0; siglongjmp(g_initJmp, 1); }
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
