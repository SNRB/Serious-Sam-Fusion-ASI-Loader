/* ASI loader as msimg32.dll.

   Sam2017_SeriousEditor.exe and Sam2017_Unrestricted.exe import MSIMG32.dll (AlphaBlend), which is not one of the
   system's "known DLLs", so Windows loads this file when it is placed next to the exe. Its exports forward to the
   GDI32 functions that the system msimg32.dll forwards to (AlphaBlend -> GdiAlphaBlend, ...).

   While the program is being loaded (DllMain), the loader puts a jump at the program's entry point. When the program
   starts, the jump leads here first: the entry point bytes are restored, every *.asi plugin in the program folder
   (and in its "scripts" and "plugins" subfolders) is loaded, then the program's real entry point runs. So the plugins
   run before the C runtime of the program initializes, the same moment as other ASI loaders.
   compile: see build.sh */
#include "common.h"

static u8 *g_entry;
static u8 g_saved[12];
static BOOL g_hooked;
static Text g_t;
static WCHAR g_dir[1100], g_pat[1200], g_path[1400];
static WIN32_FIND_DATAW g_fd;

static void log_line(const char *s, const WCHAR *w, const char *s2, s64 num, BOOL with_num) {
    t_clear(&g_t);
    t_str(&g_t, "[ASI loader] "); t_str(&g_t, s);
    if (w) t_wstr(&g_t, w);
    if (s2) t_str(&g_t, s2);
    if (with_num) t_dec(&g_t, num);
    t_str(&g_t, "\r\n");
    log_write(&g_t, FALSE);
}

static void load_folder(const WCHAR *sub) {
    wcopy(g_pat, 1200, g_dir);
    wcat(g_pat, 1200, sub);
    wcat(g_pat, 1200, L"*.asi");
    HANDLE f = FindFirstFileW(g_pat, &g_fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (g_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!wends_with_i(g_fd.cFileName, L".asi")) continue;      /* "*.asi" also matches 8.3 names of *.asix */
        wcopy(g_path, 1400, g_dir);
        wcat(g_path, 1400, sub);
        wcat(g_path, 1400, g_fd.cFileName);
        HMODULE m = LoadLibraryW(g_path);
        if (m) log_line("loaded ", g_path, NULL, 0, FALSE);
        else log_line("could not load ", g_path, ", error ", GetLastError(), TRUE);
    } while (FindNextFileW(f, &g_fd));
    FindClose(f);
}

static void load_plugins(void) {
    static BOOL done;
    if (done) return;
    done = TRUE;
    wdir_of(g_dir, 1100, g_exePath);
    load_folder(L"");
    load_folder(L"scripts\\");
    load_folder(L"plugins\\");
}

static void write_code(u8 *at, const u8 *bytes, u32 n) {
    DWORD old, old2;
    if (VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy(at, bytes, n);
        VirtualProtect(at, n, old, &old2);
        FlushInstructionCache(GetCurrentProcess(), at, n);
    }
}

/* reached through the jump at the program entry point, as the entry point itself (rcx = PEB) */
static DWORD WINAPI entry_hook(void *peb) {
    write_code(g_entry, g_saved, sizeof g_saved);
    log_line("program starting, loading plugins", NULL, NULL, 0, FALSE);
    /* mark the start-up window: plugins loaded now run before the C runtime's static initializers, which is the
       moment features that patch engine registration need. Cleared again right after, so a plugin loaded later
       (e.g. injected into the running program) can tell it is too late. */
    SetEnvironmentVariableW(L"SAM2017_ASI_WINDOW", L"1");
    load_plugins();
    SetEnvironmentVariableW(L"SAM2017_ASI_WINDOW", NULL);
    return ((DWORD (WINAPI *)(void *))g_entry)(peb);
}

static BOOL install_hook(void) {
    const u8 *nt = g_exe + *(const u32 *)(g_exe + 0x3c);
    if (*(const u32 *)nt != 0x4550 || *(const u16 *)(nt + 24) != 0x20b) return FALSE;   /* "PE\0\0", PE32+ */
    u32 ep = *(const u32 *)(nt + 24 + 16);
    if (!ep) return FALSE;
    g_entry = (u8 *)g_exe + ep;
    memcpy(g_saved, g_entry, sizeof g_saved);
    u8 jump[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};        /* mov rax, entry_hook ; jmp rax */
    u64 to = (u64)&entry_hook;
    memcpy(jump + 2, &to, 8);
    DWORD old, old2;
    if (!VirtualProtect(g_entry, sizeof jump, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    memcpy(g_entry, jump, sizeof jump);
    VirtualProtect(g_entry, sizeof jump, old, &old2);
    FlushInstructionCache(GetCurrentProcess(), g_entry, sizeof jump);
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, void *reserved) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    init_exe_info();
    t_clear(&g_t);
    t_str(&g_t, "[ASI loader] "); t_time(&g_t); t_str(&g_t, " "); t_wstr(&g_t, g_exePath); t_str(&g_t, "\r\n");
    log_write(&g_t, TRUE);
    /* reserved != NULL: loaded with the program (static import) -> wait for its entry point;
       NULL: loaded later by LoadLibrary -> the program already runs, load the plugins now */
    if (reserved && install_hook()) {
        g_hooked = TRUE;
    } else {
        log_line(reserved ? "could not hook the entry point, loading plugins now" : "loaded late, loading plugins now",
                 NULL, NULL, 0, FALSE);
        load_plugins();
    }
    return TRUE;
}

/* exported stubs of the other two msimg32 exports (not used by anyone) */
BOOL WINAPI Proxy_DllInitialize(HINSTANCE h, DWORD reason, void *reserved) { (void)h; (void)reason; (void)reserved; return TRUE; }
void WINAPI Proxy_vSetDdrawflag(void) {}
