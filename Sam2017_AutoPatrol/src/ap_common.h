/* ap_common.h - types and constants shared by the plugin and its test */
#ifndef AP_COMMON_H
#define AP_COMMON_H

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
    u64 RegisterMetaData;   /* RegisterMetaData(const INDEX64 *stream)                     */
    u64 HandleToPtr;        /* void *HandleToPtr(ULONG h)                                  */
    u64 PtrToHandle;        /* ULONG  PtrToHandle(void *p)                                 */
    u64 IsDerivedFrom;      /* BOOL IsDerivedFrom(CDataType *dt, CDataType *base)          */
    u64 stream;             /* meta data stream that contains CPuppetProperties            */
    u64 staticInit;         /* static initializer: RegisterMetaData(stream)                */
    u64 dtPuppetProps;      /* CDataType * of CPuppetProperties (0 until registered)       */
    u64 dtMarker;           /* CDataType * of CPathMarkerEntity                            */
    u64 dtEnemyMarker;      /* CDataType * of CSS1EnemyMarkerEntity                        */
    u64 dtCoverMarker;      /* CDataType * of CCoverMarkerEntity                           */
    u64 dtBoss1, dtBoss2, dtBoss3; /* SS1 pyramid spaceship / summoner / Ugh-Zan markers   */
    u64 propsCtorTail;      /* last 3 stores of the CPuppetProperties constructor (27 b.)  */
    u64 initCall;           /* call in CPuppetEntity::InitializeFromProperties (5 bytes)   */
    u64 baseInit;           /* ... its target (CEntity part: copies the name)              */
    u64 tacticCall;         /* call HandleToPtr(tactic->en_henTacticMarker) at tactic start */
    u64 streamHash;         /* FNV-1a 64 of the original stream                            */
    u8  staticInitBytes[21];
    u8  proRegisterMetaData[16], proHandleToPtr[16], proPtrToHandle[16], proIsDerivedFrom[16],
        proBaseInit[16];
    /* layout of the original meta data stream (qword indices). The CPuppetProperties record is the same in
       every build, but the stream also holds other classes (CPuppetEntity, ...) that grew in the 2026 build. */
    u32 streamQwords;       /* including the terminating -1                 */
    u32 idxFormat;          /* CPuppetProperties format (151)               */
    u32 idxNMembers;        /* number of members (48)                       */
    u32 idxBoolRec;         /* the ep_bIsMultiSpawnerTemplate record (BOOL) */
    u32 idxInsert;          /* after the last member record (ep_fActiveRangeOverride): functions count */
} Target;

/* the longest original stream of all supported builds (0x463e in 2019, 0x469e in 2026) */
#define STREAM_QWORDS_MAX 0x4700
#define OLD_FORMAT      151
#define NEW_FORMAT      152
#define OLD_NMEMBERS    48
#define NEW_MEMBER_ID   152
#define MEMBER_OFFSET   0x184   /* 4 bytes of padding at the end of the 0x188-byte object */

/* CPuppetEntity (and CEntity) */
#define OFS_WORLD          0x30   /* CWorld *                          */
#define OFS_DEF_TACTIC     0x3b0  /* en_henDefaultTacticEntity         */
#define OFS_CUR_TACTIC     0x3b4  /* en_henCurrentTacticEntity         */
#define OFS_TACTIC_MARKER  0x3bc  /* en_henTacticMarker                */
#define OFS_INTRO_MARKER   0x3c4  /* en_henIntroMarker                 */
#define OFS_OUTRO_MARKER   0x3c8  /* en_henOutroMarker                 */
/* CPathMarkerEntity */
#define OFS_NEXT_MARKER    0x5c   /* en_henNextMarker                  */
#define OFS_PREV_MARKER    0x60   /* en_henPrevMarker                  */
/* CWorld */
#define OFS_WORLD_ENTITIES 0x68   /* wo_cenEntities: CEntity **         */
#define OFS_WORLD_COUNT    0x70   /*                 INDEX count        */
/* virtual functions of CEntity */
#define VT_GETDATATYPE     0x00
#define VT_GETPLACEMENT    0xa8   /* QuatVect *GetPlacement(QuatVect *out); position at +0x10 */

#endif
