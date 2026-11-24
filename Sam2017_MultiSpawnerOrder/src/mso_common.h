/* mso_common.h - types and constants shared by the plugin and its test */
#ifndef MSO_COMMON_H
#define MSO_COMMON_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed int         i32;
typedef signed long long   i64;
typedef __WCHAR_TYPE__     wchar_t;   /* 16-bit on Windows targets (matches L"...") */

/* ------------------------------------------------------------------------- */
/* the supported executables: editor and game, builds of 7 June 2019 and     */
/* 19 November 2026                                                          */
/* ------------------------------------------------------------------------- */
typedef struct Target {
    const wchar_t *exeName;
    u32 timeDateStamp, sizeOfImage;
    u64 RegisterMetaData;   /* RegisterMetaData(const INDEX64 *stream)                */
    u64 stream;             /* MultiSpawner.cpp meta data stream                      */
    u64 staticInit;         /* static initializer: RegisterMetaData(stream)           */
    u64 classGlobal;        /* CDataType *  of CMultiSpawnerProperties (0 until reg.) */
    u64 mspVtable;          /* CMultiSpawnerProperties vtable                         */
    u64 origGetTemplate;    /* vtable[0x130/8]                                        */
    u64 mseVtable;          /* CMultiSpawnerEntity vtable                             */
    u64 spawnerVtable;      /* CSpawnerEntity vtable                                  */
    u64 ctorInplace;        /* mov qword [rbx+0x8c],1   (11 bytes)                    */
    u64 ctorNew;            /* mov qword [rbx+0x8c],1   (11 bytes)                    */
    u64 ctorArray;          /* mov qword [rbx+0x0c],1   (8 bytes, rbx = obj+0x80)     */
    u64 spawnerCtorTail;    /* add rsp,0x20 / pop rbx / ret  (6 bytes)                */
    u64 HandleToPtr;        /* void *HandleToPtr(ULONG h)                             */
    u64 PtrToHandle;        /* ULONG  PtrToHandle(void *p)                            */
    u64 Warn;               /* WarningF(const char *fmt, ...)                         */
    u64 WarnStr;            /* "Multi spawner uses template entity that is not ..."   */
    u64 streamHash;         /* FNV-1a 64 of the original stream (253 qwords)          */
    u8  staticInitBytes[21];
    u8  proRegisterMetaData[16], proHandleToPtr[16], proPtrToHandle[16], proWarn[16],
        proGetTemplate[16];
} Target;

/* layout of the original stream (identical in all four executables) */
#define STREAM_QWORDS   253     /* including the terminating -1               */
#define IDX_FORMAT      84      /* CMultiSpawnerProperties format (5)         */
#define IDX_NMEMBERS    92      /* number of members (4)                      */
#define IDX_INSERT      129     /* start of the ep_fRndBase member record     */
#define IDX_BOOLREC     117     /* start of the ep_bRandomizeTemplateOrder record */
#define NEW_FORMAT      6
#define NEW_MEMBER_ID   6
#define MEMBER_OFFSET   0x94
#define SEED_OFFSET     0x14c   /* in CSpawnerEntity (0x150 bytes, padding)  */

#endif
