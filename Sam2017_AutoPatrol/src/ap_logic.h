/* ap_logic.h - auto patrol logic and meta data stream builder
 * (pure code: used by the plugin and by the native test program) */
#ifndef AP_LOGIC_H
#define AP_LOGIC_H
#include "ap_common.h"

typedef void *(*HandleToPtr_t)(u32 h);
typedef u32   (*PtrToHandle_t)(void *p);
typedef i32   (*IsDerivedFrom_t)(void *dt, void *base);
typedef void  (*BaseInit_t)(void *ent, void *props);
typedef void *(*GetDataType_t)(void *self);
typedef float *(*GetPlacement_t)(void *self, float *out);

static const Target   *g_T;
static HandleToPtr_t   g_HandleToPtr;
static PtrToHandle_t   g_PtrToHandle;
static IsDerivedFrom_t g_IsDerivedFrom;
static BaseInit_t      g_baseInit;

static volatile u32 g_applied;      /* number of times a nearest marker was assigned */

/* ------------------------------------------------------------------------- */
/* small engine helpers                                                      */
/* ------------------------------------------------------------------------- */
static void *Global(u64 g) { return g ? *(void *volatile *)g : 0; }

static void *TypeOf(void *obj)
{
    return ((GetDataType_t)(*(void ***)obj)[VT_GETDATATYPE / 8])(obj);
}

/* position of an entity (QuatVect: quaternion, then the position at +0x10) */
static void Position(void *ent, float *xyz)
{
    float buf[16] __attribute__((aligned(16)));
    const float *qv = ((GetPlacement_t)(*(void ***)ent)[VT_GETPLACEMENT / 8])(ent, buf);
    if (!qv) qv = buf;
    xyz[0] = qv[4]; xyz[1] = qv[5]; xyz[2] = qv[6];
}

/* the entity behind a handle, if it is a path marker (CPathMarkerEntity or derived) */
static u8 *MarkerOf(u32 h)
{
    if (!h) return 0;
    u8 *m = (u8 *)g_HandleToPtr(h);
    void *mk = Global(g_T->dtMarker);
    if (!m || !mk || !g_IsDerivedFrom(TypeOf(m), mk)) return 0;
    return m;
}

/* ------------------------------------------------------------------------- */
/* characters with "Auto patrol" checked                                     */
/* ------------------------------------------------------------------------- */
/* The properties of spawned characters are deleted right after the
   character is created, so the setting is remembered here, per character
   entity: open addressing on the entity pointer, with the entity's handle to
   recognise entries of deleted entities (whose memory may be reused). */
#define AP_TAB 8192                       /* power of two */
typedef struct APSlot { u64 ent; u32 handle; u32 state; } APSlot;   /* state: 0 free, 1 used, 2 deleted */
static APSlot  g_tabA[AP_TAB], g_tabB[AP_TAB];
static APSlot *g_tab = g_tabA;
static volatile u32 g_used;
static u32     g_deleted;
static u32     g_full;                    /* the table was full at least once */
static volatile long g_lock;

static void Lock(void)
{
    while (__sync_lock_test_and_set(&g_lock, 1))
        while (g_lock) __builtin_ia32_pause();
}
static void Unlock(void) { __sync_lock_release(&g_lock); }

static u32 SlotOf(u64 p)
{
    p ^= p >> 33; p *= 0xff51afd7ed558ccdULL; p ^= p >> 33;
    return (u32)p & (AP_TAB - 1);
}

static APSlot *FindSlot(APSlot *tab, u64 ent)
{
    u32 i = SlotOf(ent);
    for (u32 k = 0; k < AP_TAB; k++, i = (i + 1) & (AP_TAB - 1)) {
        APSlot *s = &tab[i];
        if (s->state == 0) return 0;
        if (s->state == 1 && s->ent == ent) return s;
    }
    return 0;
}

static APSlot *FreeSlot(APSlot *tab, u64 ent)
{
    u32 i = SlotOf(ent);
    for (u32 k = 0; k < AP_TAB; k++, i = (i + 1) & (AP_TAB - 1))
        if (tab[i].state != 1) return &tab[i];
    return 0;
}

/* drop the entries of entities that no longer exist, and the deleted marks */
static void Rebuild(void)
{
    APSlot *src = g_tab, *dst = (g_tab == g_tabA) ? g_tabB : g_tabA;
    for (u32 i = 0; i < AP_TAB; i++) dst[i].state = 0;
    u32 n = 0;
    for (u32 i = 0; i < AP_TAB; i++) {
        APSlot *s = &src[i];
        if (s->state != 1 || g_HandleToPtr(s->handle) != (void *)s->ent) continue;
        APSlot *d = FreeSlot(dst, s->ent);
        d->ent = s->ent; d->handle = s->handle; d->state = 1;
        n++;
    }
    g_tab = dst;
    g_used = n;
    g_deleted = 0;
}

static void AP_Set(u8 *ent, int on)
{
    if (!on && !g_used) return;           /* nothing to remove */
    u32 h = on ? g_PtrToHandle(ent) : 0;
    Lock();
    APSlot *s = FindSlot(g_tab, (u64)ent);
    if (on) {
        if (s) s->handle = h;
        else {
            if ((g_used + g_deleted + 1) * 4 > AP_TAB * 3) Rebuild();
            if ((g_used + 1) * 4 > AP_TAB * 3) g_full = 1;     /* too many: this one patrols normally */
            else {
                s = FreeSlot(g_tab, (u64)ent);
                if (s->state == 2) g_deleted--;
                s->ent = (u64)ent; s->handle = h; s->state = 1;
                g_used++;
            }
        }
    } else if (s) {
        s->state = 2; g_used--; g_deleted++;
    }
    Unlock();
}

static int AP_Get(u8 *ent)
{
    int r = 0;
    Lock();
    APSlot *s = FindSlot(g_tab, (u64)ent);
    if (s) {
        if (g_HandleToPtr(s->handle) == (void *)ent) r = 1;
        else { s->state = 2; g_used--; g_deleted++; }   /* an older entity at the same address */
    }
    Unlock();
    return r;
}

/* ------------------------------------------------------------------------- */
/* which entities count as markers                                           */
/* ------------------------------------------------------------------------- */
/* 0: not a candidate
   1: one of the engine's own marker classes (Path marker, Enemy marker (SS1),
      Cover marker): must be part of a path (have a next or a previous marker)
   2: another class derived from CPathMarkerEntity (e.g. the MultiPath marker),
      which may keep its links elsewhere: always a candidate
   The SS1 boss markers (pyramid spaceship, summoner, Ugh-Zan) are not. */
#define DTK 1024                          /* cache: data type -> kind (power of two) */
typedef struct DtKind { void *volatile dt; volatile i32 kind; } DtKind;
static DtKind g_dtk[DTK];
static u32    g_ndtk;

static u32 DtSlot(void *dt)
{
    u64 p = (u64)dt;
    p ^= p >> 29; p *= 0xbf58476d1ce4e5b9ULL; p ^= p >> 32;
    return (u32)p & (DTK - 1);
}

static i32 MarkerKind(void *dt)
{
    u32 h = DtSlot(dt);
    for (u32 n = 0; n < DTK; n++, h = (h + 1) & (DTK - 1)) {
        void *d = g_dtk[h].dt;
        if (d == dt) return g_dtk[h].kind;   /* kind is written before dt */
        if (!d) break;
    }
    i32 k = 0;
    void *mk = Global(g_T->dtMarker);
    if (dt && mk && g_IsDerivedFrom(dt, mk)) {
        void *b1 = Global(g_T->dtBoss1), *b2 = Global(g_T->dtBoss2), *b3 = Global(g_T->dtBoss3);
        if ((b1 && g_IsDerivedFrom(dt, b1)) || (b2 && g_IsDerivedFrom(dt, b2)) || (b3 && g_IsDerivedFrom(dt, b3)))
            k = 0;
        else if (dt == mk || dt == Global(g_T->dtEnemyMarker) || dt == Global(g_T->dtCoverMarker))
            k = 1;
        else
            k = 2;
    }
    if (dt && g_ndtk < DTK / 2) {
        Lock();
        h = DtSlot(dt);
        for (u32 n = 0; n < DTK; n++, h = (h + 1) & (DTK - 1)) {
            if (g_dtk[h].dt == dt) break;
            if (!g_dtk[h].dt) {
                g_dtk[h].kind = k;
                __sync_synchronize();
                g_dtk[h].dt = dt;
                g_ndtk++;
                break;
            }
        }
        Unlock();
    }
    return k;
}

/* ------------------------------------------------------------------------- */
/* nearest marker                                                            */
/* ------------------------------------------------------------------------- */
#define MAXPATH 128

/* append the markers of the path that starts at m (following "next marker") */
static u32 CollectPath(u8 *m, u8 **out, u32 n, u32 cap)
{
    for (u32 k = 0; m && k < MAXPATH && n < cap; k++) {
        for (u32 j = 0; j < n; j++)
            if (out[j] == m) return n;    /* loop, or already listed */
        out[n++] = m;
        m = MarkerOf(*(u32 *)(m + OFS_NEXT_MARKER));
    }
    return n;
}

static u8 *FindNearestMarker(u8 *pup)
{
    u8 *world = *(u8 **)(pup + OFS_WORLD);
    if (!world) return 0;
    u8 **ents = *(u8 ***)(world + OFS_WORLD_ENTITIES);
    i32 cnt = *(i32 *)(world + OFS_WORLD_COUNT);
    if (!ents || cnt <= 0) return 0;

    float p[3];
    Position(pup, p);

    /* never the character's own intro or outro path */
    u8 *ex[2 * MAXPATH];
    u32 nex = 0;
    nex = CollectPath(MarkerOf(*(u32 *)(pup + OFS_INTRO_MARKER)), ex, nex, 2 * MAXPATH);
    nex = CollectPath(MarkerOf(*(u32 *)(pup + OFS_OUTRO_MARKER)), ex, nex, 2 * MAXPATH);

    u8 *best = 0;
    float bestD = 3.0e38f;
    for (i32 i = 0; i < cnt; i++) {
        u8 *e = ents[i];
        if (!e || e == pup) continue;
        i32 kind = MarkerKind(TypeOf(e));
        if (!kind) continue;
        float q[3];
        Position(e, q);
        float dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2];
        float d = dx * dx + dy * dy + dz * dz;
        if (!(d < bestD)) continue;       /* (also skips NaN) */
        if (kind == 1 && !g_HandleToPtr(*(u32 *)(e + OFS_NEXT_MARKER)) &&
            !g_HandleToPtr(*(u32 *)(e + OFS_PREV_MARKER)))
            continue;                     /* a lone marker, not part of a path */
        u32 j = 0;
        while (j < nex && ex[j] != e) j++;
        if (j < nex) continue;
        best = e;
        bestD = d;
    }
    return best;
}

/* ------------------------------------------------------------------------- */
/* the two hooks                                                             */
/* ------------------------------------------------------------------------- */
/* Replaces the call of the CEntity part at the start of
   CPuppetEntity::InitializeFromProperties(props): every character (placed in
   the level or spawned) is initialized from its properties here. */
static void AP_InitHook(u8 *ent, u8 *props)
{
    g_baseInit(ent, props);
    int on = 0;
    void *pp = Global(g_T->dtPuppetProps);
    if (ent && props && pp && g_IsDerivedFrom(TypeOf(props), pp))
        on = *(i32 *)(props + MEMBER_OFFSET) != 0;
    AP_Set(ent, on);
}

/* Replaces  HandleToPtr(tactic->en_henTacticMarker)  where CCharacterMoverManager
   starts a tactic (desired tactic -> current tactic). The engine then takes
     the tactic's own marker if there is one, else the character's
     en_henTacticMarker (if set), as the first marker (en_penCurrentPathMarker).
   For an Auto patrol character starting its own tactic, the nearest marker
   becomes its tactic marker, and NULL is returned so that the engine takes it. */
static void *AP_TacticMarkerHook(u32 hTacticMarker, u8 *mgr)
{
    void *orig = g_HandleToPtr(hTacticMarker);
    if (!g_used || !mgr) return orig;
    u8 *pup = *(u8 **)mgr;                /* cmm_penOwner */
    if (!pup || !AP_Get(pup)) return orig;
    u32 hDef = *(u32 *)(pup + OFS_DEF_TACTIC);
    if (!hDef || *(u32 *)(pup + OFS_CUR_TACTIC) != hDef) return orig;   /* not its own tactic */
    u8 *m = FindNearestMarker(pup);
    if (!m) return orig;
    *(u32 *)(pup + OFS_TACTIC_MARKER) = g_PtrToHandle(m);
    g_applied++;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* the new meta data                                                         */
/* ------------------------------------------------------------------------- */
static const char s_memDisp[] = "Auto patrol";
static const char s_memName[] = "ep_bAutoPatrol";
static const char s_memId[]   = "152";

#define MEMBER_QWORDS 12
#define NEW_QWORDS    (STREAM_QWORDS + MEMBER_QWORDS)

static u64 g_stream[NEW_QWORDS];

static u32 BuildStream(const u64 *src)
{
    u32 n = 0;
    /* original entries up to the end of the member records (members stay in offset order) */
    for (u32 i = 0; i < IDX_INSERT; i++) {
        u64 q = src[i];
        if (i == IDX_FORMAT)   q = NEW_FORMAT;
        if (i == IDX_NMEMBERS) q = OLD_NMEMBERS + 1;
        g_stream[n++] = q;
    }
    /* member: BOOL ep_bAutoPatrol  (after "Active range override", the last member) */
    g_stream[n++] = src[IDX_BOOLREC + 0];    /* type by name ...  */
    g_stream[n++] = src[IDX_BOOLREC + 1];    /* ... "BOOL"        */
    g_stream[n++] = NEW_MEMBER_ID;           /* member ID         */
    g_stream[n++] = (u64)s_memDisp;          /* display name      */
    g_stream[n++] = (u64)s_memName;          /* member name       */
    g_stream[n++] = (u64)s_memId;            /* ID string         */
    g_stream[n++] = src[IDX_BOOLREC + 6];    /* group: "" (the engine's own empty string) */
    g_stream[n++] = MEMBER_OFFSET;           /* offset            */
    g_stream[n++] = 0;                       /* flags             */
    g_stream[n++] = 0;
    g_stream[n++] = 0;
    g_stream[n++] = 0;                       /* no attributes     */
    /* rest of the original stream, including the terminating -1 */
    for (u32 i = IDX_INSERT; i < STREAM_QWORDS; i++) g_stream[n++] = src[i];
    return n;
}

#endif
