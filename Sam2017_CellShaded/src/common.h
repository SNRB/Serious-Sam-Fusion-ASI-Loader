/* Shared helpers of the ASI loader and the plugins: memory functions, text building, log file, message box.
   Everything runs on the main thread while the program starts, so static buffers are fine. */
#pragma once
#include "win_min.h"

/* One self-referential pointer in writable data: its initializer is an absolute address, so the linker emits a base
   relocation and a .reloc section. The code itself is position independent (RIP relative), but without a .reloc
   directory Windows will only map the module at its preferred base - and the loader plus three plugins would all want
   the same one. With a .reloc present the loader relocates each to a free address instead. (volatile + used so it is
   kept; static so the two-translation-unit winmm build does not get a duplicate symbol.) */
static void *volatile g_reloc_anchor __attribute__((used)) = (void *)&g_reloc_anchor;

/* the compiler may emit calls to these */
static __attribute__((used)) void *memcpy(void *d, const void *s, SIZE_T n) {
    u8 *a = d; const u8 *b = s;
    while (n--) *a++ = *b++;
    return d;
}
static __attribute__((used)) void *memset(void *d, int c, SIZE_T n) {
    u8 *a = d;
    while (n--) *a++ = (u8)c;
    return d;
}
static __attribute__((used)) int memcmp(const void *x, const void *y, SIZE_T n) {
    const u8 *a = x, *b = y;
    for (; n; n--, a++, b++)
        if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}

/* ------------------------------------------------------------------ wide strings */
static u32 wlen(const WCHAR *s) { u32 n = 0; while (s[n]) n++; return n; }
static void wcopy(WCHAR *d, u32 cap, const WCHAR *s) {
    u32 i = 0;
    for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}
static void wcat(WCHAR *d, u32 cap, const WCHAR *s) { u32 n = wlen(d); if (n < cap) wcopy(d + n, cap - n, s); }
static WCHAR wlower(WCHAR c) { return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c; }
static BOOL wieq(const WCHAR *a, const WCHAR *b) {
    for (; *a && *b; a++, b++)
        if (wlower(*a) != wlower(*b)) return FALSE;
    return *a == *b;
}
static BOOL wends_with_i(const WCHAR *s, const WCHAR *suffix) {
    u32 n = wlen(s), m = wlen(suffix);
    return n >= m && wieq(s + n - m, suffix);
}
/* compares a wide string with an ASCII string, ignoring case */
static BOOL wieq_ascii(const WCHAR *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (wlower(*a) != wlower((WCHAR)(u8)*b)) return FALSE;
    return *a == 0 && *b == 0;
}
static const WCHAR *wbase_name(const WCHAR *p) {
    const WCHAR *b = p;
    for (; *p; p++)
        if (*p == '\\' || *p == '/') b = p + 1;
    return b;
}
/* directory part with the trailing backslash */
static void wdir_of(WCHAR *d, u32 cap, const WCHAR *path) {
    wcopy(d, cap, path);
    WCHAR *b = (WCHAR *)wbase_name(d);
    *b = 0;
}

/* ------------------------------------------------------------------ text (UTF-8) */
typedef struct { char b[4096]; u32 n; } Text;
static void t_clear(Text *t) { t->n = 0; t->b[0] = 0; }
static void t_ch(Text *t, char c) { if (t->n + 1 < sizeof t->b) { t->b[t->n++] = c; t->b[t->n] = 0; } }
static void t_str(Text *t, const char *s) { while (*s) t_ch(t, *s++); }
static void t_wstr(Text *t, const WCHAR *s) {
    for (; *s; s++) {
        u32 c = *s;
        if (c >= 0xd800 && c < 0xdc00 && s[1] >= 0xdc00 && s[1] < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (s[1] - 0xdc00); s++; }
        if (c < 0x80) t_ch(t, (char)c);
        else if (c < 0x800) { t_ch(t, (char)(0xc0 | c >> 6)); t_ch(t, (char)(0x80 | (c & 0x3f))); }
        else if (c < 0x10000) { t_ch(t, (char)(0xe0 | c >> 12)); t_ch(t, (char)(0x80 | ((c >> 6) & 0x3f))); t_ch(t, (char)(0x80 | (c & 0x3f))); }
        else { t_ch(t, (char)(0xf0 | c >> 18)); t_ch(t, (char)(0x80 | ((c >> 12) & 0x3f))); t_ch(t, (char)(0x80 | ((c >> 6) & 0x3f))); t_ch(t, (char)(0x80 | (c & 0x3f))); }
    }
}
static void t_hex(Text *t, u64 v) {
    char buf[20]; int n = 0;
    do { buf[n++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
    t_str(t, "0x");
    while (n) t_ch(t, buf[--n]);
}
static void t_dec(Text *t, s64 v) {
    char buf[24]; int n = 0; u64 u = v < 0 ? (u64)-v : (u64)v;
    if (v < 0) t_ch(t, '-');
    do { buf[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) t_ch(t, buf[--n]);
}
static void t_dec2(Text *t, u32 v) { t_ch(t, (char)('0' + v / 10 % 10)); t_ch(t, (char)('0' + v % 10)); }

/* ------------------------------------------------------------------ the program */
static WCHAR g_exePath[1024];
static const u8 *g_exe;                /* image base of the program (module handle) */

static void init_exe_info(void) {
    g_exe = (const u8 *)GetModuleHandleW(NULL);
    if (!GetModuleFileNameW(NULL, g_exePath, 1024)) g_exePath[0] = 0;
}

/* ------------------------------------------------------------------ log file: <program folder>\<program name>.asi.log */
static WCHAR g_logPath[1100];
static void log_init_path(void) {
    wcopy(g_logPath, 1100, g_exePath);
    WCHAR *b = (WCHAR *)wbase_name(g_logPath);
    WCHAR *dot = NULL;
    for (WCHAR *p = b; *p; p++) if (*p == '.') dot = p;
    if (dot) *dot = 0;
    wcat(g_logPath, 1100, L".asi.log");
}
static void log_write(const Text *t, BOOL truncate) {
    if (!g_logPath[0]) log_init_path();
    HANDLE f = CreateFileW(g_logPath, truncate ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           truncate ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, t->b, t->n, &w, NULL);
    CloseHandle(f);
}
static void t_time(Text *t) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    t_dec(t, st.wYear); t_ch(t, '-'); t_dec2(t, st.wMonth); t_ch(t, '-'); t_dec2(t, st.wDay); t_ch(t, ' ');
    t_dec2(t, st.wHour); t_ch(t, ':'); t_dec2(t, st.wMinute); t_ch(t, ':'); t_dec2(t, st.wSecond);
}

/* ------------------------------------------------------------------ message box (user32 is already loaded by the program) */
typedef int (WINAPI *MessageBoxW_t)(HWND, const WCHAR *, const WCHAR *, u32);
static WCHAR g_msgW[4096];
static void message_box(const WCHAR *title, const Text *msg) {
    /* UTF-8 -> UTF-16 */
    u32 n = 0;
    for (u32 i = 0; i < msg->n && n + 2 < 4096;) {
        u8 c = (u8)msg->b[i];
        u32 cp, len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; len = 2; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; len = 3; }
        else { cp = c & 0x07; len = 4; }
        for (u32 k = 1; k < len && i + k < msg->n; k++) cp = (cp << 6) | ((u8)msg->b[i + k] & 0x3f);
        i += len;
        if (cp >= 0x10000) { cp -= 0x10000; g_msgW[n++] = (WCHAR)(0xd800 + (cp >> 10)); g_msgW[n++] = (WCHAR)(0xdc00 + (cp & 0x3ff)); }
        else g_msgW[n++] = (WCHAR)cp;
    }
    g_msgW[n] = 0;
    OutputDebugStringW(g_msgW);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) u = LoadLibraryW(L"user32.dll");
    MessageBoxW_t mb = u ? (MessageBoxW_t)GetProcAddress(u, "MessageBoxW") : NULL;
    if (mb) mb(NULL, g_msgW, title, MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
}
