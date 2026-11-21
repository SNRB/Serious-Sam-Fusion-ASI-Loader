/* winmm.dll host variant: the loader (loader.c) plus pass-through exports for the five WINMM functions the editor
   imports (timeBeginPeriod, timeEndPeriod, joyGetNumDevs, joyGetDevCapsA, joyGetPosEx).

   Each export forwards to the genuine winmm.dll in the Windows system folder, resolved lazily on first use from its
   absolute path (so it never finds this proxy by the bare name). The proxy therefore adds nothing to the program's
   audio/timer/joystick behaviour; it only serves to get the loader mapped at launch.

   This covers the EDITOR. The game (Sam2017_Unrestricted.exe) imports many more winmm functions (wave/mixer), so a
   winmm proxy is not a drop-in for the game - use the msimg32 loader there (both exes import msimg32). */
#include "common.h"

typedef u32 (WINAPI *FN_period)(u32);
typedef u32 (WINAPI *FN_numdevs)(void);
typedef u32 (WINAPI *FN_devcaps)(u64, void *, u32);
typedef u32 (WINAPI *FN_posex)(u32, void *);

#define MMSYSERR_NODRIVER 6

static HMODULE g_real;
static HMODULE real_winmm(void) {
    if (!g_real) {
        WCHAR p[1024];
        u32 n = GetSystemDirectoryW(p, 1000);
        if (!n || n >= 1000) return NULL;
        wcat(p, 1024, L"\\winmm.dll");
        g_real = LoadLibraryW(p);
    }
    return g_real;
}
static FARPROC proc(const char *name) {
    HMODULE m = real_winmm();
    return m ? GetProcAddress(m, name) : NULL;
}

u32 WINAPI wm_timeBeginPeriod(u32 period) {
    static FN_period f;
    if (!f) f = (FN_period)proc("timeBeginPeriod");
    return f ? f(period) : 0;
}
u32 WINAPI wm_timeEndPeriod(u32 period) {
    static FN_period f;
    if (!f) f = (FN_period)proc("timeEndPeriod");
    return f ? f(period) : 0;
}
u32 WINAPI wm_joyGetNumDevs(void) {
    static FN_numdevs f;
    if (!f) f = (FN_numdevs)proc("joyGetNumDevs");
    return f ? f() : 0;
}
u32 WINAPI wm_joyGetDevCapsA(u64 id, void *caps, u32 cb) {
    static FN_devcaps f;
    if (!f) f = (FN_devcaps)proc("joyGetDevCapsA");
    return f ? f(id, caps, cb) : MMSYSERR_NODRIVER;
}
u32 WINAPI wm_joyGetPosEx(u32 id, void *info) {
    static FN_posex f;
    if (!f) f = (FN_posex)proc("joyGetPosEx");
    return f ? f(id, info) : MMSYSERR_NODRIVER;
}
