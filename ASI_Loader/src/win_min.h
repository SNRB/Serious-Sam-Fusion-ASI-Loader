/* The few Windows declarations the loader and the plugins use (no SDK needed).
   Compiled with clang --target=x86_64-pc-windows-msvc: long is 32-bit, wchar_t is 16-bit, one calling convention. */
#pragma once
#ifndef WIN_MIN_H           /* the Linux test harness defines this and supplies its own shims instead */
#define WIN_MIN_H

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned char BOOLEAN;
typedef unsigned long long SIZE_T, ULONG_PTR, DWORD64, u64;
typedef long long s64;
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned short WCHAR;   /* = wchar_t on Windows */
typedef void *HANDLE, *HMODULE, *HINSTANCE, *HWND, *FARPROC;

#define WINAPI
#define IMPORT __declspec(dllimport)
#define TRUE 1
#define FALSE 0
#define NULL ((void *)0)

#define DLL_PROCESS_ATTACH 1
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define MEM_FREE 0x10000
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define GENERIC_WRITE 0x40000000
#define FILE_APPEND_DATA 0x0004
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define CREATE_ALWAYS 2
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define INVALID_HANDLE_VALUE ((HANDLE)(s64)-1)
#define MB_ICONWARNING 0x30
#define MB_SETFOREGROUND 0x10000
#define MB_TOPMOST 0x40000

typedef struct {
    void *BaseAddress;
    void *AllocationBase;
    DWORD AllocationProtect;
    WORD PartitionId;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION;

typedef struct { DWORD lo, hi; } FILETIME;
typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef struct { DWORD BeginAddress, EndAddress, UnwindData; } RUNTIME_FUNCTION;

IMPORT HMODULE WINAPI GetModuleHandleW(const WCHAR *name);
IMPORT DWORD WINAPI GetModuleFileNameW(HMODULE m, WCHAR *buf, DWORD n);
IMPORT HMODULE WINAPI LoadLibraryW(const WCHAR *name);
IMPORT FARPROC WINAPI GetProcAddress(HMODULE m, const char *name);
IMPORT BOOL WINAPI DisableThreadLibraryCalls(HMODULE m);
IMPORT void *WINAPI VirtualAlloc(void *addr, SIZE_T size, DWORD type, DWORD prot);
IMPORT BOOL WINAPI VirtualFree(void *addr, SIZE_T size, DWORD type);
IMPORT SIZE_T WINAPI VirtualQuery(const void *addr, MEMORY_BASIC_INFORMATION *mbi, SIZE_T n);
IMPORT BOOL WINAPI VirtualProtect(void *addr, SIZE_T size, DWORD prot, DWORD *old);
IMPORT BOOL WINAPI FlushInstructionCache(HANDLE process, const void *addr, SIZE_T size);
IMPORT HANDLE WINAPI GetCurrentProcess(void);
IMPORT HANDLE WINAPI CreateFileW(const WCHAR *name, DWORD access, DWORD share, void *sa, DWORD disp, DWORD flags, HANDLE tmpl);
IMPORT BOOL WINAPI WriteFile(HANDLE f, const void *buf, DWORD n, DWORD *written, void *ov);
IMPORT BOOL WINAPI CloseHandle(HANDLE h);
IMPORT HANDLE WINAPI FindFirstFileW(const WCHAR *pattern, WIN32_FIND_DATAW *fd);
IMPORT BOOL WINAPI FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd);
IMPORT BOOL WINAPI FindClose(HANDLE h);
IMPORT DWORD WINAPI GetEnvironmentVariableW(const WCHAR *name, WCHAR *buf, DWORD n);
IMPORT BOOL WINAPI SetEnvironmentVariableW(const WCHAR *name, const WCHAR *value);
IMPORT void WINAPI OutputDebugStringW(const WCHAR *s);
IMPORT DWORD WINAPI GetLastError(void);
IMPORT void WINAPI GetLocalTime(SYSTEMTIME *t);
IMPORT BOOLEAN WINAPI RtlAddFunctionTable(RUNTIME_FUNCTION *table, DWORD count, DWORD64 base);
IMPORT BOOLEAN WINAPI RtlDeleteFunctionTable(RUNTIME_FUNCTION *table);
IMPORT DWORD WINAPI GetSystemDirectoryW(WCHAR *buf, DWORD n);

#endif /* WIN_MIN_H */
