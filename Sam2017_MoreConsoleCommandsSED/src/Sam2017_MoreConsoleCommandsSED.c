/*
 * Sam2017_MoreConsoleCommandsSED.asi
 * -----------------------------------
 * ASI plugin for Serious Editor of Serious Sam Fusion 2017
 * (Sam2017_SeriousEditor.exe, build of 7 June 2019).
 *
 * Declares console variables whose code exists in the editor but which are not
 * declared to its console (custom commands, credits to Nigmas):
 *
 *   hud_bShowGeometryInfo          BOOL   0x142AB9874
 *   res_bSaveInTextFormat          INDEX  0x141EFB2D8  0..2, saved to SeriousEditor.ini
 *   scr_bUnsecureScripting         BOOL   0x142A72FD8
 *   prj_bUnlockAutoShotgunScope    BOOL   0x143279248
 *   men_bMenuFrame                 BOOL   0x143289A70
 *   prj_bAllowLevelChangeInEditor  BOOL   0x143278EAC  saved to SeriousEditor.ini
 *   fxd_bShowDebug                 BOOL   0x142AC5C50
 *   fil_bReportFileOpen            BOOL   0x1431DE4C8
 *   md_bReportReadStats            BOOL   0x1431DE960  saved to SeriousEditor.ini
 *
 * and the command "Toggle metadata codenames" (Ctrl+Shift+Alt+C), which flips the
 * byte at 0x140B730F3 between 0x18 and 0x20: the function that gives the display
 * name of a metadata member then returns its code name instead, and back.
 *
 * How it works
 *  - Cvars are declared by static record tables passed to RegisterDeclarations()
 *    (0x140B2E3E0) while each engine module initializes. The call in a "Core"
 *    module initializer (0x140B6A72B) is redirected so that this plugin's table is
 *    registered right after that one, exactly like built-in cvars. Cvars with the
 *    "saved" flag (0x4) are written by the editor itself to SeriousEditor.ini when
 *    it closes and read back when it starts (that happens after the Core module).
 *  - Descriptions are the editor's own: the console tooltip and "help <cvar>" use
 *    the records the editor loads from Help/Cvars/ *_cvars.xml. The three calls of
 *    that loader (0x14075B0D6, 0x14075B281, 0x14075BFCD) are redirected; when the
 *    loader has run, every added cvar that has no record gets one whose description
 *    is "(undocumented)", and a record without any description gets that text too.
 *    A record's type and "saved" fields are made to match the declaration.
 *  - The command is registered in "CommandHandler.Global", where the editor
 *    registers its own tool commands at startup: the call that fetches that handler
 *    there (0x140160B48) is redirected, and the command is added right before them.
 *  - res_bSaveInTextFormat lives in a read-only section of the exe, so the page
 *    that holds it is made writable.
 *
 * Like the other Sam2017 plugins it must be loaded before the program starts
 * (msimg32.dll loader), it checks the exact exe build and the code it relies
 * on, never changes anything when a check fails, and writes what it did to
 * <exe name>.asi.log next to the program.
 *
 * Built freestanding (no C runtime), see build.sh.
 */

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef long long          s64;
typedef __WCHAR_TYPE__     wchar_t;
typedef u64                size_t;
typedef int                BOOL;
typedef unsigned long      DWORD;
typedef void              *HANDLE;
typedef void              *HMODULE;

#define WINAPI
#define DLLIMPORT __declspec(dllimport)

typedef struct {
    void  *BaseAddress;
    void  *AllocationBase;
    DWORD  AllocationProtect;
    u16    PartitionId;
    u16    pad0;
    size_t RegionSize;
    DWORD  State;
    DWORD  Protect;
    DWORD  Type;
    DWORD  pad1;
} MEMORY_BASIC_INFORMATION;

DLLIMPORT BOOL    WINAPI DisableThreadLibraryCalls(HMODULE);
DLLIMPORT HMODULE WINAPI GetModuleHandleW(const wchar_t *);
DLLIMPORT BOOL    WINAPI GetModuleHandleExW(DWORD, const wchar_t *, HMODULE *);
DLLIMPORT DWORD   WINAPI GetModuleFileNameW(HMODULE, wchar_t *, DWORD);
DLLIMPORT DWORD   WINAPI GetEnvironmentVariableW(const wchar_t *, wchar_t *, DWORD);
DLLIMPORT BOOL    WINAPI SetEnvironmentVariableW(const wchar_t *, const wchar_t *);
DLLIMPORT void   *WINAPI VirtualAlloc(void *, size_t, DWORD, DWORD);
DLLIMPORT BOOL    WINAPI VirtualFree(void *, size_t, DWORD);
DLLIMPORT BOOL    WINAPI VirtualProtect(void *, size_t, DWORD, DWORD *);
DLLIMPORT size_t  WINAPI VirtualQuery(const void *, MEMORY_BASIC_INFORMATION *, size_t);
DLLIMPORT BOOL    WINAPI FlushInstructionCache(HANDLE, const void *, size_t);
DLLIMPORT HANDLE  WINAPI GetCurrentProcess(void);
DLLIMPORT DWORD   WINAPI GetLastError(void);
DLLIMPORT HANDLE  WINAPI CreateFileW(const wchar_t *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
DLLIMPORT BOOL    WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
DLLIMPORT BOOL    WINAPI CloseHandle(HANDLE);
DLLIMPORT void    WINAPI OutputDebugStringW(const wchar_t *);
DLLIMPORT HMODULE WINAPI LoadLibraryW(const wchar_t *);
DLLIMPORT void   *WINAPI GetProcAddress(HMODULE, const char *);

#define DLL_PROCESS_ATTACH        1
#define MEM_COMMIT                0x1000
#define MEM_RESERVE               0x2000
#define MEM_RELEASE               0x8000
#define MEM_FREE                  0x10000
#define PAGE_READONLY             0x02
#define PAGE_READWRITE            0x04
#define PAGE_WRITECOPY            0x08
#define PAGE_EXECUTE_READ         0x20
#define PAGE_EXECUTE_READWRITE    0x40
#define PAGE_EXECUTE_WRITECOPY    0x80
#define FILE_APPEND_DATA          0x0004
#define FILE_SHARE_READ_WRITE     0x0003
#define OPEN_ALWAYS               4
#define FILE_ATTRIBUTE_NORMAL     0x80
#define INVALID_HANDLE            ((HANDLE)(s64)-1)
#define MB_ICONWARNING            0x00000030
#define MB_SETFOREGROUND          0x00010000
#define MB_TOPMOST                0x00040000
#define GET_MODULE_HANDLE_EX_FLAG_PIN          0x1
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4

/* ------------------------------------------------------------------------- */
/* What the plugin knows about the exe                                        */
/* ------------------------------------------------------------------------- */

#include "exe_expect.h"   /* identity, hashes of relied-on code, original call bytes */

#define EXE_NAME                    L"Sam2017_SeriousEditor.exe"
#define RVA_SECURITY_COOKIE         0x2A32688u            /* __security_cookie */
#define SECURITY_COOKIE_DEFAULT     0x00002B992DDFA232ULL /* value before the CRT starts */

#define RVA_RegisterDeclarations    0x0B2E3E0u  /* void (const u64 *table)                              */
#define RVA_LoadCvarsHelp           0x075B430u  /* void (void): loads Help/Cvars/ *_cvars.xml, once      */
#define RVA_CvarsHelpLoaded         0x2AE59D0u  /* int, set to 1 by LoadCvarsHelp                       */
#define RVA_CvarDescArray           0x2903E28u  /* CStaticStackArray<CCvarDesc> (CvarsHelp records)      */
#define RVA_CvarDescCtor            0x075ACF0u  /* CCvarDesc::CCvarDesc()                                */
#define RVA_CvarDescArrayExpand     0x075AF20u  /* CStaticStackArray<CCvarDesc>::Expand(int)             */
#define RVA_CStringAssign           0x0B25F30u  /* CString &CString::operator=(const char *)             */
#define RVA_GetGlobalCmdHandler     0x1611F00u  /* CCommandHandler *(void): "CommandHandler.Global"      */
#define RVA_RegisterCommand         0x1610600u  /* (handler, id, tooltip, execute, update, extra)        */
#define RVA_RegisterShortcut        0x1610430u  /* (handler, command id, shortcut id, key, event, extra) */
#define RVA_ConsoleInfo             0x0B1FFD0u  /* console message of type CMT_INFO (format "%1"...)    */
#define RVA_MemberNameGetter        0x0B730F0u  /* mov rcx,[rcx+18h] ; jmp IdToString                   */
#define RVA_MemberNameOffset        0x0B730F3u  /* that 18h (display name); 20h is the code name         */

/* Engine declaration record flags, as the editor's own cvars use them:
   0x10002 = "pure" cvar listed in the console (md_bLoadIncompatibleTypes),
   0x4     = saved (0x10006 like hud_bShowCoords): the editor writes it to
             SeriousEditor.ini when it closes and reads it back when it starts. */
#define CVAR_FLAGS                  0x10002ULL
#define CVAR_FLAG_SAVED             0x4ULL

/* The console variables this plugin adds. To add another one, add a line here:
   name, address in the exe minus 0x140000000, engine type name ("BOOL", "INDEX",
   "FLOAT"), 1 to save it to SeriousEditor.ini, and optional "min"/"max" limits
   (the console clamps values typed for it, like for the built-in efx_iBloom).
   The description shown is the editor's own (Help/Cvars/ *_cvars.xml), or
   "(undocumented)". A variable in a read-only section is made writable. */
typedef struct CVarDef {
    const char    *name;
    unsigned int   rva;
    const char    *type;
    int            saved;
    const char    *minValue;    /* or 0 */
    const char    *maxValue;    /* or 0 */
} CVarDef;

static const CVarDef g_cvars[] = {
    { "hud_bShowGeometryInfo",         0x2AB9874u, "BOOL",  0, 0,   0   },  /* 0x142AB9874, .data  */
    { "res_bSaveInTextFormat",         0x1EFB2D8u, "INDEX", 1, "0", "2" },  /* 0x141EFB2D8, .rdata */
    { "scr_bUnsecureScripting",        0x2A72FD8u, "BOOL",  0, 0,   0   },  /* 0x142A72FD8, .data  */
    { "prj_bUnlockAutoShotgunScope",   0x3279248u, "BOOL",  0, 0,   0   },  /* 0x143279248, .data  */
    { "men_bMenuFrame",                0x3289A70u, "BOOL",  0, 0,   0   },  /* 0x143289A70, .data  */
    { "prj_bAllowLevelChangeInEditor", 0x3278EACu, "BOOL",  1, 0,   0   },  /* 0x143278EAC, .data  */
    { "fxd_bShowDebug",                0x2AC5C50u, "BOOL",  0, 0,   0   },  /* 0x142AC5C50, .data  */
    { "fil_bReportFileOpen",           0x31DE4C8u, "BOOL",  0, 0,   0   },  /* 0x1431DE4C8, .data  */
    { "md_bReportReadStats",           0x31DE960u, "BOOL",  1, 0,   0   },  /* 0x1431DE960, .data  */
};
#define NUM_CVARS ((int)(sizeof(g_cvars) / sizeof(g_cvars[0])))

#define HELP_UNDOCUMENTED     "(undocumented)"      /* what the editor's help window shows then */
#define HELP_UNDOCUMENTED_W   L"(undocumented)"
#define HELP_PURITY           "pure"                /* CVAR_FLAGS */

/* The key-bind command */
#define CMD_TOGGLE_CODENAMES  "Toggle metadata codenames"   /* command id and description */
#define CMD_TOGGLE_CODENAMES_W L"Toggle metadata codenames"
#define KEY_CTRL_SHIFT_ALT_C  0xE0000042u   /* Ctrl 0x80000000 | Alt 0x40000000 | Shift 0x20000000 | KID_C 0x42 */
#define CAE_KEYPRESS          2             /* command activation event */
#define NAME_DISPLAY          0x18          /* byte at 0x140B730F3: display name (original) */
#define NAME_CODE             0x20          /*                      code name               */

#define MOD_NAME          L"MoreConsoleCommandsSED"
#define MOD_NAME_A        "MoreConsoleCommandsSED"
#define APPLIED_ENV_VAR   L"SAM2017_MoreConsoleCommandsSED"
#define STUB_MAGIC        0x43373153u   /* "S17C" */

/* Engine types used by the help records */
typedef struct CString { char *str; } CString;
typedef struct CCvarDesc {             /* 0x58 bytes, field names from the exe's metadata */
    CString cvd_strName;
    CString cvd_strSource;
    CString cvd_strBriefComment;
    CString cvd_strDetailComment;
    CString cvd_strSaved;
    CString cvd_strPurity;
    CString cvd_strMetaConst;
    CString cvd_strType;
    CString cvd_strFunction;
    CString cvd_strParams;
    CString cvd_strUserName;
} CCvarDesc;
typedef struct CvarDescArray { CCvarDesc *data; int count; int allocated; int step; } CvarDescArray;

typedef void (*PFN_RegisterDeclarations)(const u64 *table);
typedef u64  (*PFN_Pass4)(u64, u64, u64, u64);
typedef CCvarDesc *(*PFN_CvarDescCtor)(CCvarDesc *self);
typedef void (*PFN_CvarDescArrayExpand)(CvarDescArray *self, int newAllocated);
typedef CString *(*PFN_CStringAssign)(CString *self, const char *str);
typedef int  (*PFN_CommandFn)(void *);
typedef void (*PFN_RegisterCommand)(void *handler, const char *id, const char *tooltip,
                                    PFN_CommandFn execute, PFN_CommandFn update, u64 extra);
typedef void (*PFN_RegisterShortcut)(void *handler, const char *commandId, const char *shortcutId,
                                     u32 key, u32 activationEvent, u64 extra);
typedef void (*PFN_ConsoleInfo)(const char *text);

_Static_assert(sizeof(CCvarDesc) == 0x58, "CCvarDesc must be 0x58 bytes");
_Static_assert(sizeof(MEMORY_BASIC_INFORMATION) == 48, "MEMORY_BASIC_INFORMATION layout");
_Static_assert(sizeof(g_sites) / sizeof(g_sites[0]) == NUM_SITES, "exe_expect.h site list");

/* ------------------------------------------------------------------------- */
/* Freestanding helpers                                                       */
/* ------------------------------------------------------------------------- */

void *memset(void *dst, int c, size_t n)
{
    volatile u8 *d = (volatile u8 *)dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    volatile u8 *d = (volatile u8 *)dst;
    const volatile u8 *s = (const volatile u8 *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static int StrEqA(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char LowerA(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* ASCII case-insensitive, like the editor's help lookup; NULL counts as "" */
static int StrEqNoCaseA(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    while (*a && LowerA(*a) == LowerA(*b)) { a++; b++; }
    return LowerA(*a) == LowerA(*b);
}

static int IsEmptyA(const char *s) { return !s || !*s; }

static wchar_t LowerW(wchar_t c) { return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c; }

static int StrEqNoCaseW(const wchar_t *a, const wchar_t *b)
{
    while (*a && LowerW(*a) == LowerW(*b)) { a++; b++; }
    return LowerW(*a) == LowerW(*b);
}

static const wchar_t *FileNamePart(const wchar_t *path)
{
    const wchar_t *p = path;
    for (; *path; path++) if (*path == L'\\' || *path == L'/') p = path + 1;
    return p;
}

static u64 Fnv1a64(const u8 *p, size_t n)
{
    u64 h = 0xCBF29CE484222325ULL;
    while (n--) { h ^= *p++; h *= 0x100000001B3ULL; }
    return h;
}

/* Text builder (UTF-16), converted to UTF-8 for the log file. */
#define TEXT_MAX 4096
typedef struct Text { wchar_t buf[TEXT_MAX]; unsigned len; } Text;

static void TReset(Text *t) { t->len = 0; t->buf[0] = 0; }
static void TW(Text *t, const wchar_t *s)
{
    while (s && *s && t->len + 1 < TEXT_MAX) t->buf[t->len++] = *s++;
    t->buf[t->len] = 0;
}
static void TA(Text *t, const char *s)   /* ASCII only */
{
    while (s && *s && t->len + 1 < TEXT_MAX) t->buf[t->len++] = (wchar_t)(u8)*s++;
    t->buf[t->len] = 0;
}
static void THex(Text *t, u64 v)
{
    char tmp[24]; int n = 0;
    const char *digits = "0123456789abcdef";
    do { tmp[n++] = digits[v & 15]; v >>= 4; } while (v);
    TA(t, "0x");
    while (n--) { char c[2] = { tmp[n], 0 }; TA(t, c); }
}
static void TDec(Text *t, u64 v)
{
    char tmp[24]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n--) { char c[2] = { tmp[n], 0 }; TA(t, c); }
}

/* ------------------------------------------------------------------------- */
/* Globals                                                                    */
/* ------------------------------------------------------------------------- */

static HMODULE  g_self;
static u8      *g_exe;                    /* image base of the program */
static wchar_t  g_exePath[1100];
static wchar_t  g_selfPath[1100];
static wchar_t  g_logPath[1100];
static const wchar_t *g_exeFile;          /* file name parts */
static const wchar_t *g_selfFile;

static Text     g_line;                   /* one log line / message */
static Text     g_box;                    /* message box text */
static char     g_utf8[TEXT_MAX * 3 + 8];

static u8      *g_stub;                   /* 64 KB block near the program code */
static u64      g_table[10 * NUM_CVARS + 1];
static volatile int g_cvarsRegistered;
static volatile int g_helpFixed;
static volatile int g_commandAdded;

/* ------------------------------------------------------------------------- */
/* Log file and message box (same conventions as the other Sam2017 plugins)   */
/* ------------------------------------------------------------------------- */

static void BuildLogPath(void)
{
    /* <exe dir>\<exe name without extension>.asi.log, shared with the loader */
    unsigned i = 0, dot = 0, nameStart = 0;
    for (; g_exePath[i] && i < 1000; i++) {
        g_logPath[i] = g_exePath[i];
        if (g_exePath[i] == L'\\' || g_exePath[i] == L'/') { nameStart = i + 1; dot = 0; }
        else if (g_exePath[i] == L'.') dot = i;
    }
    if (dot <= nameStart) dot = i;
    g_logPath[dot] = 0;
    {
        const wchar_t *ext = L".asi.log";
        unsigned j = 0;
        while (ext[j]) { g_logPath[dot + j] = ext[j]; j++; }
        g_logPath[dot + j] = 0;
    }
}

static unsigned ToUtf8(const wchar_t *s, char *out, unsigned cap)
{
    unsigned n = 0;
    while (*s && n + 4 < cap) {
        u32 c = (u16)*s++;
        if (c >= 0xD800 && c <= 0xDBFF && *s >= 0xDC00 && *s <= 0xDFFF)
            c = 0x10000 + ((c - 0xD800) << 10) + ((u16)*s++ - 0xDC00);
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { out[n++] = (char)(0xC0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) {
            out[n++] = (char)(0xE0 | (c >> 12)); out[n++] = (char)(0x80 | ((c >> 6) & 63));
            out[n++] = (char)(0x80 | (c & 63));
        } else {
            out[n++] = (char)(0xF0 | (c >> 18)); out[n++] = (char)(0x80 | ((c >> 12) & 63));
            out[n++] = (char)(0x80 | ((c >> 6) & 63)); out[n++] = (char)(0x80 | (c & 63));
        }
    }
    out[n] = 0;
    return n;
}

/* Writes "[<plugin file>] <text>\r\n" to the log and to the debugger. */
static void LogText(const wchar_t *text)
{
    static Text line;
    HANDLE h;
    DWORD written;
    unsigned n;

    TReset(&line);
    TW(&line, L"[");
    TW(&line, g_selfFile);
    TW(&line, L"] ");
    TW(&line, text);
    TW(&line, L"\r\n");
    OutputDebugStringW(line.buf);

    if (!g_logPath[0]) return;
    n = ToUtf8(line.buf, g_utf8, sizeof(g_utf8));
    h = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ_WRITE, 0, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE) return;
    WriteFile(h, g_utf8, n, &written, 0);
    CloseHandle(h);
}

static void ShowBox(const wchar_t *text)
{
    typedef int (WINAPI *PFN_MessageBoxW)(HANDLE, const wchar_t *, const wchar_t *, u32);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    PFN_MessageBoxW mb;
    if (!user32) user32 = LoadLibraryW(L"user32.dll");
    if (!user32) return;
    mb = (PFN_MessageBoxW)GetProcAddress(user32, "MessageBoxW");
    if (mb) mb(0, text, L"Sam2017 mods", MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
}

/* Logs "NOT APPLIED: <reason>" and tells the user. */
static void NotApplied(const wchar_t *reason)
{
    TReset(&g_line);
    TW(&g_line, L"NOT APPLIED: ");
    TW(&g_line, reason);
    LogText(g_line.buf);

    TReset(&g_box);
    TW(&g_box, g_selfFile);
    TW(&g_box, L" was not applied to ");
    TW(&g_box, g_exeFile);
    TW(&g_box, L".\n\n");
    TW(&g_box, reason);
    TW(&g_box, L"\n\nThe program continues without this mod. Details are in the file ");
    TW(&g_box, FileNamePart(g_logPath));
    TW(&g_box, L" next to the program.");
    ShowBox(g_box.buf);
}

/* ------------------------------------------------------------------------- */
/* Memory helpers                                                             */
/* ------------------------------------------------------------------------- */

static int IsReadable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    const u8 *a = (const u8 *)p, *end = a + n;
    while (a < end) {
        if (!VirtualQuery(a, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
        if (mbi.Protect & 0x100 /* PAGE_GUARD */) return 0;
        a = (const u8 *)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

/* Target of the "call rel32" at site (site[0] == 0xE8). */
static u8 *CallTarget(const u8 *site)
{
    s64 rel = (s64)(int)((u32)site[1] | ((u32)site[2] << 8) | ((u32)site[3] << 16) | ((u32)site[4] << 24));
    return (u8 *)site + 5 + rel;
}

/* Reserves a 64 KB block within +-2 GB of every patched call, below the exe image
   first (the other Sam2017 mods use fixed blocks right after the image). */
static u8 *AllocNearProgram(void)
{
    const u64 gran = 0x10000, room = 0x7FFF0000ULL;
    u64 lo = 0, hi = ~0ULL, a;
    unsigned i;
    MEMORY_BASIC_INFORMATION mbi;

    for (i = 0; i < NUM_SITES; i++) {
        u64 next = (u64)(g_exe + g_sites[i].rva + 5);
        u64 l = next > room ? next - room : 0, h = next + room;
        if (l > lo) lo = l;
        if (h < hi) hi = h;
    }
    lo = (lo + gran - 1) & ~(gran - 1);

    /* 1) downwards from just below the image */
    a = (((u64)g_exe) & ~(gran - 1)) - gran;
    while (a >= lo && a + gran <= hi) {
        if (!VirtualQuery((void *)a, &mbi, sizeof(mbi))) break;
        if (mbi.State == MEM_FREE) {
            if ((u64)mbi.BaseAddress + mbi.RegionSize >= a + gran) {
                void *p = VirtualAlloc((void *)a, gran, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (p) return (u8 *)p;
            }
            if (a < lo + gran) break;
            a -= gran;
        } else {
            /* skip the whole allocation (allocations start on 64 KB boundaries) */
            u64 below = (u64)mbi.AllocationBase & ~(gran - 1);
            if (below < lo + gran) break;
            a = below - gran;
        }
    }
    /* 2) upwards, leaving 64 MB after the image for the other mods' blocks */
    a = ((u64)g_exe + EXE_SIZEOFIMAGE + 0x4000000ULL + gran - 1) & ~(gran - 1);
    while (a + gran <= hi) {
        if (!VirtualQuery((void *)a, &mbi, sizeof(mbi))) break;
        if (mbi.State == MEM_FREE) {
            if ((u64)mbi.BaseAddress + mbi.RegionSize >= a + gran) {
                void *p = VirtualAlloc((void *)a, gran, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (p) return (u8 *)p;
            }
            a += gran;
        } else {
            a = ((u64)mbi.BaseAddress + mbi.RegionSize + gran - 1) & ~(gran - 1);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Hook 1: declaring the cvars inside the Core module                         */
/* ------------------------------------------------------------------------- */

static void BuildDeclarationTable(void)
{
    /* Same layout as the editor's own static declaration tables (kind 2 = variable):
       kind, name, user name, address, type kind (1 = by name), type name, flags,
       attribute count, attribute (name, value) pairs, ... , -1 */
    int i, k = 0;
    for (i = 0; i < NUM_CVARS; i++) {
        const CVarDef *cv = &g_cvars[i];
        g_table[k++] = 2;
        g_table[k++] = (u64)cv->name;
        g_table[k++] = (u64)"";
        g_table[k++] = (u64)(g_exe + cv->rva);
        g_table[k++] = 1;
        g_table[k++] = (u64)cv->type;
        g_table[k++] = CVAR_FLAGS | (cv->saved ? CVAR_FLAG_SAVED : 0);
        g_table[k++] = (cv->minValue ? 1 : 0) + (cv->maxValue ? 1 : 0);
        if (cv->minValue) { g_table[k++] = (u64)"min"; g_table[k++] = (u64)cv->minValue; }
        if (cv->maxValue) { g_table[k++] = (u64)"max"; g_table[k++] = (u64)cv->maxValue; }
    }
    g_table[k] = ~0ULL;
}

/* Replaces "call RegisterDeclarations" in the Core module initializer. */
static void Hook_CoreDeclarations(const u64 *table)
{
    PFN_RegisterDeclarations registerDeclarations =
        (PFN_RegisterDeclarations)(g_exe + RVA_RegisterDeclarations);

    registerDeclarations(table);              /* what the program did */

    if (!g_cvarsRegistered) {
        int i;
        g_cvarsRegistered = 1;
        BuildDeclarationTable();
        registerDeclarations(g_table);

        TReset(&g_line);
        TW(&g_line, L"console variables declared:");
        for (i = 0; i < NUM_CVARS; i++) {
            TW(&g_line, i ? L", " : L" ");
            if (g_cvars[i].saved) TW(&g_line, L"saved ");
            TA(&g_line, g_cvars[i].type);
            TW(&g_line, L" ");
            TA(&g_line, g_cvars[i].name);
            if (g_cvars[i].minValue || g_cvars[i].maxValue) {
                TW(&g_line, L" (");
                TA(&g_line, g_cvars[i].minValue ? g_cvars[i].minValue : "");
                TW(&g_line, L"..");
                TA(&g_line, g_cvars[i].maxValue ? g_cvars[i].maxValue : "");
                TW(&g_line, L")");
            }
            TW(&g_line, L" at ");
            THex(&g_line, (u64)(g_exe + g_cvars[i].rva));
        }
        LogText(g_line.buf);
    }
}

/* ------------------------------------------------------------------------- */
/* Hook 2: help records, after the editor loaded its own                      */
/* ------------------------------------------------------------------------- */

static CCvarDesc *FindHelp(CvarDescArray *arr, const char *name)
{
    int i;
    for (i = 0; i < arr->count; i++)                    /* first match, like the editor */
        if (arr->data[i].cvd_strName.str && StrEqNoCaseA(arr->data[i].cvd_strName.str, name))
            return &arr->data[i];
    return 0;
}

static void FixHelpRecords(void)
{
    CvarDescArray *arr = (CvarDescArray *)(g_exe + RVA_CvarDescArray);
    PFN_CvarDescCtor ctor = (PFN_CvarDescCtor)(g_exe + RVA_CvarDescCtor);
    PFN_CvarDescArrayExpand expand = (PFN_CvarDescArrayExpand)(g_exe + RVA_CvarDescArrayExpand);
    PFN_CStringAssign assign = (PFN_CStringAssign)(g_exe + RVA_CStringAssign);
    int i, fromFiles = 0;

    TReset(&g_line);
    TW(&g_line, L"console help:");
    for (i = 0; i < NUM_CVARS; i++) {
        const CVarDef *cv = &g_cvars[i];
        const char *saved = cv->saved ? "true" : "false";
        CCvarDesc *d = FindHelp(arr, cv->name);

        TW(&g_line, i ? L"; " : L" ");
        TA(&g_line, cv->name);
        if (!d) {
            /* not in the help files: a record like the editor's, without a description */
            if (arr->count + 1 > arr->allocated) {
                int step = arr->step > 0 ? arr->step : 0x400;
                expand(arr, (arr->count / step + 1) * step);
            }
            d = &arr->data[arr->count];
            ctor(d);
            arr->count++;
            assign(&d->cvd_strName, cv->name);
            assign(&d->cvd_strSource, "Sam2017_MoreConsoleCommandsSED.asi");
            assign(&d->cvd_strBriefComment, HELP_UNDOCUMENTED);
            assign(&d->cvd_strSaved, saved);
            assign(&d->cvd_strPurity, HELP_PURITY);
            assign(&d->cvd_strMetaConst, "false");
            assign(&d->cvd_strType, cv->type);
            assign(&d->cvd_strFunction, "false");
            TW(&g_line, L" not in the help files, shown as " HELP_UNDOCUMENTED_W);
            continue;
        }
        fromFiles++;
        TW(&g_line, L" from the help files");
        if (IsEmptyA(d->cvd_strBriefComment.str) && IsEmptyA(d->cvd_strDetailComment.str)) {
            assign(&d->cvd_strBriefComment, HELP_UNDOCUMENTED);
            TW(&g_line, L", no description there, shown as " HELP_UNDOCUMENTED_W);
        }
        if (!StrEqNoCaseA(d->cvd_strType.str, cv->type)) {
            TW(&g_line, L", type ");
            TA(&g_line, IsEmptyA(d->cvd_strType.str) ? "(none)" : d->cvd_strType.str);
            TW(&g_line, L" shown as ");
            TA(&g_line, cv->type);
            assign(&d->cvd_strType, cv->type);
        }
        if (!StrEqNoCaseA(d->cvd_strSaved.str, saved)) {
            TW(&g_line, cv->saved ? L", shown as saved" : L", shown as not saved");
            assign(&d->cvd_strSaved, saved);
        }
    }
    LogText(g_line.buf);
    (void)fromFiles;
}

/* Replaces the three "call LoadCvarsHelp" (list of cvars, "help" command, help lookup
   used by the console tooltip). The loader reads the XML files only once. */
static u64 Hook_LoadCvarsHelp(u64 a, u64 b, u64 c, u64 d)
{
    volatile int *loaded = (volatile int *)(g_exe + RVA_CvarsHelpLoaded);
    int wasLoaded = *loaded;
    u64 r = ((PFN_Pass4)(g_exe + RVA_LoadCvarsHelp))(a, b, c, d);
    if (*loaded && (!wasLoaded || !g_helpFixed)) {   /* records (re)loaded just now */
        g_helpFixed = 1;
        FixHelpRecords();
    }
    return r;
}

/* ------------------------------------------------------------------------- */
/* Hook 3: the "Toggle metadata codenames" command                            */
/* ------------------------------------------------------------------------- */

static void ConsoleInfo(const char *text)
{
    ((PFN_ConsoleInfo)(g_exe + RVA_ConsoleInfo))(text);
}

/* Command callback: 1 = handled. */
static int Cmd_ToggleMetadataCodenames(void *unused)
{
    u8 *p = g_exe + RVA_MemberNameOffset;
    u8 now = *(volatile u8 *)p, next;
    DWORD old, tmp;
    (void)unused;

    if (now == NAME_DISPLAY) next = NAME_CODE;
    else if (now == NAME_CODE) next = NAME_DISPLAY;
    else {
        ConsoleInfo("Toggle metadata codenames: the editor code at 0x140B730F0 was changed by something else, "
                    "nothing toggled.\n");
        return 1;
    }
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) {
        ConsoleInfo("Toggle metadata codenames: VirtualProtect failed, nothing toggled.\n");
        return 1;
    }
    *(volatile u8 *)p = next;
    VirtualProtect(p, 1, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
    ConsoleInfo(next == NAME_CODE
                ? "Metadata codenames ON: metadata members are shown by their code names.\n"
                : "Metadata codenames OFF: metadata members are shown by their display names.\n");
    return 1;
}

/* Replaces "call GetGlobalCommandHandler" in the editor's startup, right before it
   registers its own tool commands (Free resource stock, ...) in that handler. */
static u64 Hook_EditorGlobalCommands(u64 a, u64 b, u64 c, u64 d)
{
    u64 handler = ((PFN_Pass4)(g_exe + RVA_GetGlobalCmdHandler))(a, b, c, d);
    if (handler && !g_commandAdded) {
        g_commandAdded = 1;
        ((PFN_RegisterCommand)(g_exe + RVA_RegisterCommand))(
            (void *)handler, CMD_TOGGLE_CODENAMES, CMD_TOGGLE_CODENAMES, Cmd_ToggleMetadataCodenames, 0, 0);
        ((PFN_RegisterShortcut)(g_exe + RVA_RegisterShortcut))(
            (void *)handler, CMD_TOGGLE_CODENAMES, "", KEY_CTRL_SHIFT_ALT_C, CAE_KEYPRESS, 0);

        TReset(&g_line);
        TW(&g_line, L"command \"" CMD_TOGGLE_CODENAMES_W L"\" registered in CommandHandler.Global with the "
                    L"shortcut Ctrl+Shift+Alt+C (metadata names currently: ");
        TW(&g_line, g_exe[RVA_MemberNameOffset] == NAME_CODE ? L"code names)" : L"display names)");
        LogText(g_line.buf);
    }
    return handler;
}

/* ------------------------------------------------------------------------- */
/* Installing                                                                 */
/* ------------------------------------------------------------------------- */

static int ProgramHasStarted(void)
{
    wchar_t v[8];
    if (GetEnvironmentVariableW(L"SAM2017_ASI_WINDOW", v, 8)) return 0;     /* msimg32.dll loader */
    /* any other loader: before the C runtime starts the /GS cookie still has its default */
    return *(volatile u64 *)(g_exe + RVA_SECURITY_COOKIE) != SECURITY_COOKIE_DEFAULT;
}

static int IsOurStub(const u8 *p)
{
    const u8 *block = (const u8 *)((u64)p & ~0xFFFFULL);
    return IsReadable(block, 0x40) && *(const u32 *)block == STUB_MAGIC &&
           StrEqA((const char *)block + 8, MOD_NAME_A);
}

/* The member display-name getter, with either name selected:
   48 8B 49 18|20  mov rcx,[rcx+18h|20h]  ;  E9 F7 78 FB FF  jmp IdToString */
static int NameGetterIsKnown(void)
{
    static const u8 code[9] = { 0x48, 0x8B, 0x49, 0x00, 0xE9, 0xF7, 0x78, 0xFB, 0xFF };
    const u8 *p = g_exe + RVA_MemberNameGetter;
    int i;
    if (!IsReadable(p, 9)) return 0;
    for (i = 0; i < 9; i++) if (i != 3 && p[i] != code[i]) return 0;
    return p[3] == NAME_DISPLAY || p[3] == NAME_CODE;
}

static void WriteJumpStub(u8 *at, void *target)
{
    /* jmp qword ptr [rip+0] ; dq target */
    at[0] = 0xFF; at[1] = 0x25; at[2] = 0; at[3] = 0; at[4] = 0; at[5] = 0;
    *(u64 *)(at + 6) = (u64)target;
}

static int PatchCall(u8 *site, u8 *newTarget, DWORD *err)
{
    DWORD old, tmp;
    s64 rel = (s64)(newTarget - (site + 5));
    if (rel != (s64)(int)rel) { *err = 0; return 0; }
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) { *err = GetLastError(); return 0; }
    *(volatile u32 *)(site + 1) = (u32)(int)rel;
    VirtualProtect(site, 5, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 1;
}

static void RestoreCall(u8 *site, const u8 *orig)
{
    DWORD old, tmp;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(site, orig, 5);
    VirtualProtect(site, 5, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
}

static void RestoreVarPages(const DWORD *oldProt, unsigned n)
{
    unsigned j;
    DWORD tmp;
    for (j = 0; j < n; j++)
        if (oldProt[j]) VirtualProtect(g_exe + g_cvars[j].rva, 4, oldProt[j], &tmp);
}

static void Install(void)
{
    static wchar_t envBuf[8];
    static Text reason;
    void *hookFns[NUM_SITES];
    DWORD err = 0, oldProt[NUM_CVARS];
    unsigned i, j;

    hookFns[SITE_CORE_DECLARATIONS] = (void *)Hook_CoreDeclarations;
    hookFns[SITE_HELP_CVAR_LIST]    = (void *)Hook_LoadCvarsHelp;
    hookFns[SITE_HELP_COMMAND]      = (void *)Hook_LoadCvarsHelp;
    hookFns[SITE_HELP_LOOKUP]       = (void *)Hook_LoadCvarsHelp;
    hookFns[SITE_EDITOR_COMMANDS]   = (void *)Hook_EditorGlobalCommands;

    /* one copy per process */
    if (GetEnvironmentVariableW(APPLIED_ENV_VAR, envBuf, 8)) {
        LogText(L"already applied by another copy of this plugin, skipped");
        return;
    }

    if (ProgramHasStarted()) {
        NotApplied(L"It was loaded after the program had started, too late for its changes. "
                   L"Install the msimg32.dll loader that comes with it (copy msimg32.dll - or winmm.dll - "
                   L"next to the exe, with this plugin in the plugins folder), or use an ASI loader that "
                   L"loads plugins before the program starts.");
        return;
    }

    /* the calls it redirects must be untouched (checked first: a copy that already
       redirected them changed the bytes around them too) */
    for (i = 0; i < NUM_SITES; i++) {
        u8 *site = g_exe + g_sites[i].rva;
        int same = 1;
        for (j = 0; j < 5; j++) if (site[j] != g_sites[i].orig[j]) same = 0;
        if (same) continue;
        if (site[0] == 0xE8 && IsOurStub(CallTarget(site))) {
            LogText(L"already applied by another copy of this plugin, skipped");
            return;
        }
        TReset(&reason);
        TW(&reason, L"The program code it changes was already changed by something else (at ");
        THex(&reason, (u64)site);
        TW(&reason, L"). Another mod, or the same mod in another form, may be installed.");
        NotApplied(reason.buf);
        return;
    }

    /* the code this plugin calls or depends on must be the original */
    for (i = 0; i < sizeof(g_verify) / sizeof(g_verify[0]); i++) {
        const u8 *p = g_exe + g_verify[i].rva;
        if (!IsReadable(p, g_verify[i].len) || Fnv1a64(p, g_verify[i].len) != g_verify[i].hash) {
            TReset(&reason);
            TW(&reason, L"The program code it relies on is different from what it expects (at ");
            THex(&reason, (u64)p);
            TW(&reason, L"). Another mod may have changed it.");
            NotApplied(reason.buf);
            return;
        }
    }
    if (!NameGetterIsKnown()) {
        TReset(&reason);
        TW(&reason, L"The program code it relies on is different from what it expects (at ");
        THex(&reason, (u64)(g_exe + RVA_MemberNameGetter));
        TW(&reason, L"). Another mod may have changed it.");
        NotApplied(reason.buf);
        return;
    }

    /* variables in a read-only section (res_bSaveInTextFormat is in .rdata) get a writable page */
    for (i = 0; i < (unsigned)NUM_CVARS; i++) {
        MEMORY_BASIC_INFORMATION mbi;
        u8 *var = g_exe + g_cvars[i].rva;
        oldProt[i] = 0;
        if (!VirtualQuery(var, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
            RestoreVarPages(oldProt, i);
            TReset(&reason);
            TW(&reason, L"Internal error: variable at ");
            THex(&reason, (u64)var);
            TW(&reason, L" out of range.");
            NotApplied(reason.buf);
            return;
        }
        if (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
            continue;
        if (!VirtualProtect(var, 4, PAGE_READWRITE, &oldProt[i])) {
            err = GetLastError();
            oldProt[i] = 0;
            RestoreVarPages(oldProt, i);
            TReset(&reason);
            TW(&reason, L"VirtualProtect on the program failed (error ");
            TDec(&reason, err);
            TW(&reason, L").");
            NotApplied(reason.buf);
            return;
        }
    }

    /* jump stubs within reach of the patched calls */
    g_stub = AllocNearProgram();
    if (!g_stub) {
        RestoreVarPages(oldProt, NUM_CVARS);
        NotApplied(L"Could not reserve memory near the program code.");
        return;
    }
    *(u32 *)g_stub = STUB_MAGIC;
    *(u32 *)(g_stub + 4) = 2;                 /* stub layout version */
    memcpy(g_stub + 8, MOD_NAME_A, sizeof(MOD_NAME_A));
    for (i = 0; i < NUM_SITES; i++) WriteJumpStub(g_stub + 0x40 + 0x10 * i, hookFns[i]);
    {
        DWORD old;
        if (!VirtualProtect(g_stub, 0x1000, PAGE_EXECUTE_READ, &old)) {
            err = GetLastError();
            VirtualFree(g_stub, 0, MEM_RELEASE);
            g_stub = 0;
            RestoreVarPages(oldProt, NUM_CVARS);
            TReset(&reason);
            TW(&reason, L"VirtualProtect failed (error ");
            TDec(&reason, err);
            TW(&reason, L").");
            NotApplied(reason.buf);
            return;
        }
        FlushInstructionCache(GetCurrentProcess(), g_stub, 0x1000);
    }

    /* redirect the calls */
    for (i = 0; i < NUM_SITES; i++) {
        if (!PatchCall(g_exe + g_sites[i].rva, g_stub + 0x40 + 0x10 * i, &err)) {
            for (j = 0; j < i; j++) RestoreCall(g_exe + g_sites[j].rva, g_sites[j].orig);
            RestoreVarPages(oldProt, NUM_CVARS);
            VirtualFree(g_stub, 0, MEM_RELEASE);
            g_stub = 0;
            TReset(&reason);
            if (err) {
                TW(&reason, L"VirtualProtect on the program failed (error ");
                TDec(&reason, err);
                TW(&reason, L").");
            } else {
                TW(&reason, L"Internal error: call at ");
                THex(&reason, (u64)(g_exe + g_sites[i].rva));
                TW(&reason, L" out of range.");
            }
            NotApplied(reason.buf);
            return;
        }
    }

    /* the hooks live in this DLL: never let it be unloaded */
    {
        HMODULE pinned;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (const wchar_t *)(void *)Install, &pinned);
    }
    SetEnvironmentVariableW(APPLIED_ENV_VAR, L"1");

    TReset(&g_line);
    TW(&g_line, L"applied to ");
    TW(&g_line, g_exeFile);
    TW(&g_line, L" (");
    TDec(&g_line, NUM_SITES);
    TW(&g_line, L" call redirect(s), stubs at ");
    THex(&g_line, (u64)g_stub);
    TW(&g_line, L"): adds");
    for (i = 0; i < (unsigned)NUM_CVARS; i++) {
        TW(&g_line, i ? L", " : L" ");
        TA(&g_line, g_cvars[i].name);
        if (oldProt[i]) TW(&g_line, L" (page made writable)");
    }
    TW(&g_line, L" and the command \"" CMD_TOGGLE_CODENAMES_W L"\" (Ctrl+Shift+Alt+C)");
    LogText(g_line.buf);
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                                */
/* ------------------------------------------------------------------------- */

BOOL WINAPI DllMain(HMODULE hinst, DWORD reason, void *reserved)
{
    const u8 *nt;
    u32 ts, sizeOfImage, checksum;
    (void)reserved;

    if (reason != DLL_PROCESS_ATTACH) return 1;
    g_self = hinst;
    DisableThreadLibraryCalls(hinst);

    g_exe = (u8 *)GetModuleHandleW(0);
    if (!GetModuleFileNameW(0, g_exePath, 1024)) g_exePath[0] = 0;
    if (!GetModuleFileNameW(hinst, g_selfPath, 1024)) {
        const wchar_t *def = L"Sam2017_MoreConsoleCommandsSED.asi";
        unsigned k = 0;
        while (def[k]) { g_selfPath[k] = def[k]; k++; }
        g_selfPath[k] = 0;
    }
    g_exeFile = FileNamePart(g_exePath);
    g_selfFile = FileNamePart(g_selfPath);
    BuildLogPath();

    nt = g_exe + *(const u32 *)(g_exe + 0x3C);
    ts = *(const u32 *)(nt + 8);
    sizeOfImage = *(const u32 *)(nt + 24 + 56);
    checksum = *(const u32 *)(nt + 24 + 64);

    if (ts == EXE_TIMEDATESTAMP && sizeOfImage == EXE_SIZEOFIMAGE && checksum == EXE_CHECKSUM &&
        (u64)g_exe == EXE_IMAGEBASE) {
        Install();
        return 1;
    }

    if (StrEqNoCaseW(g_exeFile, EXE_NAME)) {
        /* the editor, but not the build this plugin was made for */
        static wchar_t warned[8];
        TReset(&g_line);
        TW(&g_line, L"NOT APPLIED: ");
        TW(&g_line, g_exeFile);
        TW(&g_line, L" (TimeDateStamp ");
        THex(&g_line, ts);
        TW(&g_line, L", SizeOfImage ");
        THex(&g_line, sizeOfImage);
        TW(&g_line, L") is not the original executable of 7 June 2019");
        LogText(g_line.buf);
        if (!GetEnvironmentVariableW(L"SAM2017_ASI_WARNED", warned, 8)) {
            SetEnvironmentVariableW(L"SAM2017_ASI_WARNED", L"1");
            TReset(&g_box);
            TW(&g_box, L"This ");
            TW(&g_box, g_exeFile);
            TW(&g_box, L" is not the original executable of 7 June 2019 that the Sam2017 mod plugins (.asi) "
                       L"were made for. If it is a patched exe from an earlier build of these mods, restore "
                       L"the original one (Steam: Properties > Installed Files > Verify integrity of game "
                       L"files): the plugins now make the same changes in memory.\n\n"
                       L"The program continues without the mods.");
            ShowBox(g_box.buf);
        }
        return 1;
    }

    /* e.g. the game (Sam2017_Unrestricted.exe) loading the same plugins folder */
    TReset(&g_line);
    TW(&g_line, L"nothing to do: ");
    TW(&g_line, g_exeFile);
    TW(&g_line, L" (TimeDateStamp ");
    THex(&g_line, ts);
    TW(&g_line, L", SizeOfImage ");
    THex(&g_line, sizeOfImage);
    TW(&g_line, L") is not a program this plugin knows");
    LogText(g_line.buf);
    return 1;
}
