/*
 * Sam2017_EditorToolsQOLpatches.asi
 * ---------------------------------
 * ASI plugin for Serious Editor of Serious Sam Fusion 2017 (Sam2017_SeriousEditor.exe, build of
 * 7 June 2019): quality-of-life fixes for the editor's tools.  For now one:
 *
 * Skeleton editor, "Reparent" (Tool.SetBoneParent, Shift+S) and "Reparent in place"
 * (Tool.SetBoneParentInPlace, Shift+Alt+S): with several bones selected, the editor asks for the
 * new parent of the first one only ("Choose new parent for '<bone>' bone") and changes only that
 * bone.  With this plugin the new parent goes to every selected bone, in one undo step.
 *
 * Both tools run the same function in this build, 0x1419EE000 (the "in place" argument it gets is
 * not used): it takes the first bone of the selection (0x1419D9DB0), asks for the parent name
 * (0x141673DB0, with the hint list "HintList.Bones"), then in one undo step sets the bone's parent
 * name (0x1419D2780) and gives it the placement relative to its new parent that keeps it where it
 * is in the model (its placement in the model 0x1419F5630 before, the parent's after, then
 * 0x1419D27A0), and rebuilds the skeleton tree (0x1419EC460 / 0x1419EC2A0).
 *
 * The plugin points slot 30 of the two tools' vtables (the slot the editor's tool activation
 * runs) to its own function.  With one bone selected (or none) that calls the editor's function
 * unchanged.  With several it does the same as the editor for each of them, in one undo step
 * "Reparent bones", after one dialog that names them:
 *   - a selected bone that is itself the new parent keeps its parent;
 *   - so does a selected bone that the new parent hangs below (it would make a loop);
 *   - an empty name makes the bones roots, as the editor does for one bone; a name no bone of
 *     their LOD has is set as their parent name the same way (they then have no parent bone);
 *   - the console says what was done.
 * Nothing about the skeleton format changes: the bones get the parent names and placements the
 * editor itself would give them one at a time.
 *
 * It checks the exe build and the code and data it relies on, never changes anything when a
 * check fails, and writes what it did to <exe name>.asi.log next to the program.  It can be loaded
 * at any time (the slots are read whenever a tool starts).  Built freestanding (no C runtime).
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
DLLIMPORT BOOL    WINAPI VirtualProtect(void *, size_t, DWORD, DWORD *);
DLLIMPORT size_t  WINAPI VirtualQuery(const void *, MEMORY_BASIC_INFORMATION *, size_t);
DLLIMPORT DWORD   WINAPI GetLastError(void);
DLLIMPORT HANDLE  WINAPI CreateFileW(const wchar_t *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
DLLIMPORT BOOL    WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
DLLIMPORT BOOL    WINAPI CloseHandle(HANDLE);
DLLIMPORT void    WINAPI OutputDebugStringW(const wchar_t *);
DLLIMPORT HMODULE WINAPI LoadLibraryW(const wchar_t *);
DLLIMPORT void   *WINAPI GetProcAddress(HMODULE, const char *);

#define DLL_PROCESS_ATTACH        1
#define MEM_COMMIT                0x1000
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

int _fltused = 0;   /* floating point without the C runtime */

/* the compiler may emit calls to these for struct copies */
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

/* ------------------------------------------------------------------------- */
/* What the plugin knows about the exe                                        */
/* ------------------------------------------------------------------------- */

#include "exe_expect.h"   /* identity, hashes of relied-on code and data, the slots it changes */

#define EXE_NAME          L"Sam2017_SeriousEditor.exe"
#define APPLIED_ENV_VAR   L"SAM2017_EditorToolsQOLpatches"

_Static_assert(sizeof(MEMORY_BASIC_INFORMATION) == 48, "MEMORY_BASIC_INFORMATION layout");
_Static_assert(sizeof(g_slots) / sizeof(g_slots[0]) == NUM_SLOTS, "exe_expect.h slot list");

static u8 *g_exe;                    /* image base of the program */

/* the editor's functions (RVAs; hashed in exe_expect.h) */
typedef u64 IDENT;                                         /* pointer to an interned text */
typedef struct { float qx, qy, qz, qw, vx, vy, vz; } QuatVect;   /* rotation (x y z w), position */
typedef struct { char *s; } CString;

#define RVA_Reparent            0x19EE000u  /* int (void *tool, int bInPlace): the editor's Reparent (first bone)  */
#define RVA_BoneSelection       0x19D9DB0u  /* int *(int *list): handle list of the selected bones, in that order   */
#define RVA_ListRelease         0x15BFE40u  /* void (int list)                                                     */
#define RVA_ListCount           0x15BFD00u  /* int (const int *list)                                               */
#define RVA_ListItem            0x15BFD30u  /* int (const int *list, int i): a bone handle                         */
#define RVA_BoneName            0x19D2330u  /* IDENT *(IDENT *out, int bone)                                       */
#define RVA_BoneLOD             0x19D23A0u  /* int (int bone): handle of its skeleton LOD                          */
#define RVA_BoneParentName      0x19D23F0u  /* IDENT *(IDENT *out, int bone)                                       */
#define RVA_FindBone            0x19F5540u  /* int (int lod, IDENT name): bone handle or -1                        */
#define RVA_BonePlacement       0x19F5630u  /* QuatVect *(QuatVect *out, int bone, int flags): in the model         */
#define RVA_BoneParent          0x19F5730u  /* int (int bone): parent bone handle or -1                            */
#define RVA_SetBoneParent       0x19D2780u  /* void (int bone, IDENT parentName): undoable                         */
#define RVA_SetBoneRelPlacement 0x19D27A0u  /* void (int bone, const QuatVect *rel): undoable                      */
#define RVA_AskText             0x1673DB0u  /* int (const CString *title, const CString *text, CString *value,
                                               int, int, IDENT hints, int): 1 = OK                                 */
#define RVA_CStringFrom         0x0B25C50u  /* CString *(CString *out, const char *s)                              */
#define RVA_CStringFree         0x0B269A0u  /* void (CString *s)                                                   */
#define RVA_IdentFromString     0x0B2AB30u  /* IDENT *(IDENT *out, const char *s)                                  */
#define RVA_IdentText           0x0B2A9F0u  /* const char *(IDENT id)                                              */
#define RVA_UndoBegin           0x15C0510u  /* void *(void *scope, void *tool)                                     */
#define RVA_UndoCommit          0x15C0680u  /* void (void *scope, const char *name, int)                           */
#define RVA_UndoEnd             0x15C05B0u  /* void (void *scope)                                                  */
#define RVA_SkeletonTree        0x19EC460u  /* void *(void): the skeleton tree view, 0 when there is none          */
#define RVA_SkeletonTreeReset   0x19EC2A0u  /* void (void *tree): it rebuilds its list                             */
#define RVA_ConsoleInfo         0x0B1FFD0u  /* void (const char *format, ...)                                      */
#define RVA_ConsoleWarning      0x0B20970u  /* void (const char *format, ...)                                      */
#define ARG_TEXT                0xABCD0009u /* type tag of a text argument of the editor's formatter ("%1")       */

typedef int       (*PFN_Reparent)(void *tool, int inPlace);
typedef int      *(*PFN_BoneSelection)(int *list);
typedef void      (*PFN_ListRelease)(int list);
typedef int       (*PFN_ListCount)(const int *list);
typedef int       (*PFN_ListItem)(const int *list, int i);
typedef IDENT    *(*PFN_BoneIdent)(IDENT *out, int bone);
typedef int       (*PFN_BoneLOD)(int bone);
typedef int       (*PFN_FindBone)(int lod, IDENT name);
typedef QuatVect *(*PFN_BonePlacement)(QuatVect *out, int bone, int flags);
typedef int       (*PFN_BoneParent)(int bone);
typedef void      (*PFN_SetBoneParent)(int bone, IDENT parent);
typedef void      (*PFN_SetBoneRelPlacement)(int bone, const QuatVect *rel);
typedef int       (*PFN_AskText)(const CString *title, const CString *text, CString *value, int a, int b, IDENT hints, int c);
typedef CString  *(*PFN_CStringFrom)(CString *out, const char *s);
typedef void      (*PFN_CStringFree)(CString *s);
typedef IDENT    *(*PFN_IdentFromString)(IDENT *out, const char *s);
typedef const char *(*PFN_IdentText)(IDENT id);
typedef void     *(*PFN_UndoBegin)(void *scope, void *tool);
typedef void      (*PFN_UndoCommit)(void *scope, const char *name, int flag);
typedef void      (*PFN_UndoEnd)(void *scope);
typedef void     *(*PFN_SkeletonTree)(void);
typedef void      (*PFN_SkeletonTreeReset)(void *tree);
typedef void      (*PFN_Console)(const char *format, ...);

#define FN(type, rva) ((type)(void *)(g_exe + (rva)))

/* ------------------------------------------------------------------------- */
/* Freestanding helpers                                                       */
/* ------------------------------------------------------------------------- */

static int StrEqA(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

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
#define TEXT_MAX 2048
typedef struct Text { wchar_t buf[TEXT_MAX]; unsigned len; } Text;

static void TReset(Text *t) { t->len = 0; t->buf[0] = 0; }
static void TW(Text *t, const wchar_t *s)
{
    while (s && *s && t->len + 1 < TEXT_MAX) t->buf[t->len++] = *s++;
    t->buf[t->len] = 0;
}
static void TA(Text *t, const char *s)   /* ASCII / Latin-1 */
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

/* Text builder (8-bit, as the editor's texts), never overflowing */
typedef struct { char *buf; unsigned cap, len; } AText;
static void AReset(AText *t) { t->len = 0; t->buf[0] = 0; }
static void AS(AText *t, const char *s)
{
    while (s && *s && t->len + 1 < t->cap) t->buf[t->len++] = *s++;
    t->buf[t->len] = 0;
}
static void AI(AText *t, unsigned v)
{
    char tmp[12]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n--) { char c[2] = { tmp[n], 0 }; AS(t, c); }
}

/* ------------------------------------------------------------------------- */
/* Globals                                                                    */
/* ------------------------------------------------------------------------- */

static wchar_t  g_exePath[1100];
static wchar_t  g_selfPath[1100];
static wchar_t  g_logPath[1100];
static const wchar_t *g_exeFile;          /* file name parts */
static const wchar_t *g_selfFile;

static Text     g_line;                   /* one log line / message */
static Text     g_box;                    /* message box text */
static char     g_utf8[TEXT_MAX * 3 + 8];

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

/* ------------------------------------------------------------------------- */
/* Reparent with several bones selected                                       */
/* ------------------------------------------------------------------------- */

#define MAX_BONES   4096         /* bones handled by one command */
#define MAX_DEPTH   100000       /* steps up a parent chain (a skeleton may already have a loop) */
#define LIST_NAMES  8            /* bone names listed in a console message */
#define ASK_NAMES   4            /* ... and in the dialog */

enum { KEEP_NONE = 0, KEEP_SELF, KEEP_ABOVE };
static int  g_bone[MAX_BONES];
static u8   g_keep[MAX_BONES];
static char g_ask[1024], g_msg[8192], g_part[4096];

static const char *BoneName(int bone)
{
    IDENT id = 0;
    const char *s = FN(PFN_IdentText, RVA_IdentText)(*FN(PFN_BoneIdent, RVA_BoneName)(&id, bone));
    return s ? s : "";
}

/* 1 when 'bone' is 'p' or one of the bones above it */
static int IsAtOrAbove(int bone, int p)
{
    for (int k = 0; p != -1 && k < MAX_DEPTH; k++) {
        if (p == bone) return 1;
        p = FN(PFN_BoneParent, RVA_BoneParent)(p);
    }
    return 0;
}

/* placement relative to a parent that keeps the model placement 'abs' where it is (rel = parent^-1 * abs) */
static QuatVect RelativeTo(const QuatVect *parent, const QuatVect *abs)
{
    double px = -parent->qx, py = -parent->qy, pz = -parent->qz, pw = parent->qw;        /* conjugate */
    double n = px * px + py * py + pz * pz + pw * pw;
    double ax = abs->qx, ay = abs->qy, az = abs->qz, aw = abs->qw;
    double qx = pw * ax + px * aw + py * az - pz * ay;
    double qy = pw * ay - px * az + py * aw + pz * ax;
    double qz = pw * az + px * ay - py * ax + pz * aw;
    double qw = pw * aw - px * ax - py * ay - pz * az;
    double dx = abs->vx - parent->vx, dy = abs->vy - parent->vy, dz = abs->vz - parent->vz;
    /* the difference turned by the conjugate: v' = v + 2w (u x v) + 2 u x (u x v), u = (px py pz), w = pw */
    double tx = 2 * (py * dz - pz * dy), ty = 2 * (pz * dx - px * dz), tz = 2 * (px * dy - py * dx);
    double rx = dx + pw * tx + (py * tz - pz * ty);
    double ry = dy + pw * ty + (pz * tx - px * tz);
    double rz = dz + pw * tz + (px * ty - py * tx);
    QuatVect r;
    if (n > 0 && (n < 0.999999 || n > 1.000001)) {     /* not a unit quaternion: divide by its length squared */
        qx /= n; qy /= n; qz /= n; qw /= n; rx /= n; ry /= n; rz /= n;
    }
    r.qx = (float)qx; r.qy = (float)qy; r.qz = (float)qz; r.qw = (float)qw;
    r.vx = (float)rx; r.vy = (float)ry; r.vz = (float)rz;
    return r;
}

/* "'a', 'b' and 'c'" - the bones i < n with g_keep[i] == which, at most 'most' names, then "and N more" */
static unsigned NameList(AText *t, int n, int which, unsigned most)
{
    unsigned total = 0, shown = 0;
    for (int i = 0; i < n; i++) if (g_keep[i] == which) total++;
    for (int i = 0; i < n; i++) {
        if (g_keep[i] != which) continue;
        if (shown == most) {
            AS(t, " and "); AI(t, total - shown); AS(t, " more");
            break;
        }
        if (shown) AS(t, shown + 1 == total ? " and " : ", ");
        AS(t, "'"); AS(t, BoneName(g_bone[i])); AS(t, "'");
        shown++;
    }
    return total;
}

static void Say(int warning, const char *text)
{
    FN(PFN_Console, warning ? RVA_ConsoleWarning : RVA_ConsoleInfo)("%1", ARG_TEXT, text);
}

static int ReparentSelected(void *tool, int inPlace)
{
    int list = -1, n;
    FN(PFN_BoneSelection, RVA_BoneSelection)(&list);
    n = list == -1 ? 0 : FN(PFN_ListCount, RVA_ListCount)(&list);
    if (n < 2) {
        /* one bone (or none): the editor's own Reparent, which gets the selection itself */
        if (list != -1) FN(PFN_ListRelease, RVA_ListRelease)(list);
        return FN(PFN_Reparent, RVA_Reparent)(tool, inPlace);
    }
    int truncated = n > MAX_BONES;
    if (truncated) n = MAX_BONES;
    for (int i = 0; i < n; i++) { g_bone[i] = FN(PFN_ListItem, RVA_ListItem)(&list, i); g_keep[i] = KEEP_NONE; }

    /* the dialog: as the editor's, for all of them; it starts with the first bone's parent */
    AText ask = { g_ask, sizeof g_ask, 0 };
    AReset(&ask);
    AS(&ask, "Choose new parent for the ");
    AI(&ask, (unsigned)n);
    AS(&ask, " selected bones: ");
    NameList(&ask, n, KEEP_NONE, ASK_NAMES);
    CString title, text, value;
    IDENT first = 0, hints = 0;
    const char *firstParent = FN(PFN_IdentText, RVA_IdentText)(*FN(PFN_BoneIdent, RVA_BoneParentName)(&first, g_bone[0]));
    FN(PFN_CStringFrom, RVA_CStringFrom)(&title, "Reparent bones");
    FN(PFN_CStringFrom, RVA_CStringFrom)(&text, g_ask);
    FN(PFN_CStringFrom, RVA_CStringFrom)(&value, firstParent ? firstParent : "");
    FN(PFN_IdentFromString, RVA_IdentFromString)(&hints, "HintList.Bones");
    int ok = FN(PFN_AskText, RVA_AskText)(&title, &text, &value, 0, 1, hints, 0);
    FN(PFN_CStringFree, RVA_CStringFree)(&title);
    FN(PFN_CStringFree, RVA_CStringFree)(&text);
    if (!ok) {
        FN(PFN_CStringFree, RVA_CStringFree)(&value);
        FN(PFN_ListRelease, RVA_ListRelease)(list);
        return 1;
    }
    const char *newName = value.s ? value.s : "";
    IDENT parentName = 0;
    FN(PFN_IdentFromString, RVA_IdentFromString)(&parentName, newName);

    /* which bones change (decided on the skeleton as it is): not the new parent itself, and not a
       bone the new parent hangs below - each in the LOD of the bone, as the editor resolves names */
    int nChange = 0, nNoBone = 0;
    for (int i = 0; i < n; i++) {
        int b = g_bone[i];
        int p = newName[0] ? FN(PFN_FindBone, RVA_FindBone)(FN(PFN_BoneLOD, RVA_BoneLOD)(b), parentName) : -1;
        if (p == b) g_keep[i] = KEEP_SELF;
        else if (p != -1 && IsAtOrAbove(b, p)) g_keep[i] = KEEP_ABOVE;
        else { nChange++; if (p == -1 && newName[0]) nNoBone++; }
    }

    /* as the editor does for one bone: its parent name, then the placement relative to the new
       parent that keeps it in place; all in one undo step */
    if (nChange) {
        void *scope[4] = { 0, 0, 0, 0 };
        FN(PFN_UndoBegin, RVA_UndoBegin)(scope, tool);
        for (int i = 0; i < n; i++) {
            if (g_keep[i] != KEEP_NONE) continue;
            int b = g_bone[i];
            QuatVect abs, parent, rel;
            FN(PFN_BonePlacement, RVA_BonePlacement)(&abs, b, 0);
            FN(PFN_SetBoneParent, RVA_SetBoneParent)(b, parentName);
            int p = FN(PFN_BoneParent, RVA_BoneParent)(b);
            if (p == -1) rel = abs;
            else {
                FN(PFN_BonePlacement, RVA_BonePlacement)(&parent, p, 0);
                rel = RelativeTo(&parent, &abs);
            }
            FN(PFN_SetBoneRelPlacement, RVA_SetBoneRelPlacement)(b, &rel);
        }
        FN(PFN_UndoCommit, RVA_UndoCommit)(scope, "Reparent bones", 1);
        void *tree = FN(PFN_SkeletonTree, RVA_SkeletonTree)();
        if (tree) FN(PFN_SkeletonTreeReset, RVA_SkeletonTreeReset)(tree);
        FN(PFN_UndoEnd, RVA_UndoEnd)(scope);
    }

    /* what was done */
    AText m = { g_msg, sizeof g_msg, 0 }, part = { g_part, sizeof g_part, 0 };
    AReset(&m);
    if (nChange) {
        AS(&m, "Reparent bones: ");
        NameList(&m, n, KEEP_NONE, LIST_NAMES);
        if (!newName[0]) AS(&m, nChange == 1 ? " has no parent now" : " have no parent now");
        else {
            AS(&m, nChange == 1 ? " is now under '" : " are now under '");
            AS(&m, newName);
            AS(&m, "'");
        }
        AS(&m, " (kept in place)");
        if (nNoBone) {
            AS(&m, "; no bone of their skeleton LOD is named '"); AS(&m, newName);
            AS(&m, "', so they have that parent name but no parent bone");
        }
    } else {
        AS(&m, "Reparent bones - nothing done");
    }
    int self = 0, above = 0;
    for (int i = 0; i < n; i++) { if (g_keep[i] == KEEP_SELF) self++; if (g_keep[i] == KEEP_ABOVE) above++; }
    if (self || above) {
        AS(&m, nChange ? ". Not changed: " : ": ");
        if (self) {
            AReset(&part); NameList(&part, n, KEEP_SELF, LIST_NAMES); AS(&m, g_part);
            AS(&m, self == 1 ? " (it is the new parent)" : " (each is the new parent of its LOD)");
        }
        if (above) {
            if (self) AS(&m, ", ");
            AReset(&part); NameList(&part, n, KEEP_ABOVE, LIST_NAMES); AS(&m, g_part);
            AS(&m, " (the new parent hangs below ");
            AS(&m, above == 1 ? "it" : "them");
            AS(&m, ", so that would make a loop)");
        }
    }
    if (truncated) { AS(&m, ". Only the first "); AI(&m, MAX_BONES); AS(&m, " selected bones were handled"); }
    AS(&m, ".\n");
    Say(!nChange, g_msg);

    FN(PFN_CStringFree, RVA_CStringFree)(&value);
    FN(PFN_ListRelease, RVA_ListRelease)(list);
    return 1;
}

/* slot 30 of the two tools */
static int Tool_Reparent(void *tool) { return ReparentSelected(tool, 0); }
static int Tool_ReparentInPlace(void *tool) { return ReparentSelected(tool, 1); }

/* ------------------------------------------------------------------------- */
/* Installing                                                                 */
/* ------------------------------------------------------------------------- */

static u64 NewValue(unsigned i) { return i == 0 ? (u64)(void *)Tool_Reparent : (u64)(void *)Tool_ReparentInPlace; }

static void Install(void)
{
    static wchar_t envBuf[8];
    static Text reason;
    DWORD old = 0, tmp;
    unsigned i;
    u8 *lo = 0, *hi = 0;

    /* one copy per process */
    if (GetEnvironmentVariableW(APPLIED_ENV_VAR, envBuf, 8)) {
        LogText(L"already applied by another copy of this plugin, skipped");
        return;
    }

    /* the slots it changes must hold their original values */
    for (i = 0; i < NUM_SLOTS; i++) {
        const u8 *f = g_exe + g_slots[i].rva;
        if (!IsReadable(f, 8) || *(volatile const u64 *)f != g_slots[i].orig) {
            TReset(&reason);
            TW(&reason, L"The program data it changes was already changed by something else (at ");
            THex(&reason, (u64)f);
            TW(&reason, L"). Another mod, or the same mod in another form, may be installed.");
            NotApplied(reason.buf);
            return;
        }
    }

    /* the code and data this plugin depends on must be the original */
    for (i = 0; i < sizeof(g_verify) / sizeof(g_verify[0]); i++) {
        const u8 *p = g_exe + g_verify[i].rva;
        if (!IsReadable(p, g_verify[i].len) || Fnv1a64(p, g_verify[i].len) != g_verify[i].hash) {
            TReset(&reason);
            TW(&reason, L"The program code or data it relies on is different from what it expects (at ");
            THex(&reason, (u64)p);
            TW(&reason, L"). Another mod may have changed it.");
            NotApplied(reason.buf);
            return;
        }
    }

    /* the slots point to this DLL from now on: never let it be unloaded */
    {
        HMODULE pinned;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (const wchar_t *)(void *)Install, &pinned);
    }

    /* the two slots, in the read-only data of the program (one page) */
    for (i = 0; i < NUM_SLOTS; i++) {
        u8 *f = g_exe + g_slots[i].rva;
        if (!lo || f < lo) lo = f;
        if (!hi || f + 8 > hi) hi = f + 8;
    }
    if (!VirtualProtect(lo, (size_t)(hi - lo), PAGE_READWRITE, &old)) {
        DWORD err = GetLastError();
        TReset(&reason);
        TW(&reason, L"VirtualProtect on the program failed (error ");
        TDec(&reason, err);
        TW(&reason, L").");
        NotApplied(reason.buf);
        return;
    }
    for (i = 0; i < NUM_SLOTS; i++) *(volatile u64 *)(g_exe + g_slots[i].rva) = NewValue(i);
    VirtualProtect(lo, (size_t)(hi - lo), old, &tmp);
    SetEnvironmentVariableW(APPLIED_ENV_VAR, L"1");

    TReset(&g_line);
    TW(&g_line, L"applied to ");
    TW(&g_line, g_exeFile);
    TW(&g_line, L" (vtable slots ");
    THex(&g_line, (u64)(g_exe + g_slots[0].rva));
    TW(&g_line, L" and ");
    THex(&g_line, (u64)(g_exe + g_slots[1].rva));
    TW(&g_line, L"): Skeleton editor \"Reparent\" (Shift+S) and \"Reparent in place\" (Shift+Alt+S) give the new parent "
                L"to every selected bone");
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
    DisableThreadLibraryCalls(hinst);

    g_exe = (u8 *)GetModuleHandleW(0);
    if (!GetModuleFileNameW(0, g_exePath, 1024)) g_exePath[0] = 0;
    if (!GetModuleFileNameW(hinst, g_selfPath, 1024)) {
        const wchar_t *def = L"Sam2017_EditorToolsQOLpatches.asi";
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
