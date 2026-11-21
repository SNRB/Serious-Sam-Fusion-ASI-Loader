/* Native (Linux) test of the plugin's logic (character table, nearest marker,
 * the two hooks) and of the meta data stream builder, with mock engine
 * objects. Build and run: see run_tests.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/ap_logic.h"

static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- mock engine ------------------------------------------------------- */
/* handles: index | generation << 24, like the engine */
#define NH 20000
static void *g_hobj[NH];
static u8    g_hgen[NH];
static u32   g_nh = 1;
static u32   g_h2p, g_p2h;
static void *Mock_HandleToPtr(u32 h)
{
    g_h2p++;
    u32 i = h & 0xffffff;
    if (!h || i >= g_nh || (h >> 24) != g_hgen[i]) return 0;
    return g_hobj[i];
}
static u32 Mock_PtrToHandle(void *p)
{
    g_p2h++;
    if (!p) return 0;
    for (u32 i = 1; i < g_nh; i++) if (g_hobj[i] == p) return i | ((u32)g_hgen[i] << 24);
    if (g_nh >= NH) { printf("out of handles\n"); exit(2); }
    g_hobj[g_nh] = p; g_hgen[g_nh] = 1;
    return g_nh++ | (1u << 24);
}
static void Mock_Release(void *p)   /* object deleted: its handle becomes invalid */
{
    for (u32 i = 1; i < g_nh; i++) if (g_hobj[i] == p) { g_hobj[i] = 0; g_hgen[i]++; }
}

/* data types: a class tree */
typedef struct DT { const char *name; struct DT *base; } DT;
static DT dtEntity = {"CEntity", 0};
static DT dtProps = {"CEntityProperties", 0};
static DT dtPuppetProps = {"CPuppetProperties", &dtProps};
static DT dtLeggedProps = {"CLeggedCharacterProperties", &dtPuppetProps};
static DT dtOtherProps = {"CStaticModelProperties", &dtProps};
static DT dtPuppet = {"CPuppetEntity", &dtEntity};
static DT dtMarker = {"CPathMarkerEntity", &dtEntity};
static DT dtEnemy = {"CSS1EnemyMarkerEntity", &dtMarker};
static DT dtCover = {"CCoverMarkerEntity", &dtMarker};
static DT dtBoss1 = {"CSS1PyramidSpaceShipMarkerEntity", &dtMarker};
static DT dtBoss2 = {"CSS1SummonerMarkerEntity", &dtMarker};
static DT dtBoss3 = {"CSS1UghZanMarkerEntity", &dtMarker};
static DT dtMulti = {"CMultiPathMarkerEntity", &dtMarker};   /* a plugin class */
static DT dtTactic = {"CTacticEntity", &dtEntity};
static DT dtLight = {"CLightEntity", &dtEntity};
static u32 g_derivedCalls;
static i32 Mock_IsDerivedFrom(void *dt, void *base)
{
    g_derivedCalls++;
    for (DT *d = (DT *)dt; d; d = d->base) if (d == (DT *)base) return 1;
    return 0;
}
/* the engine's globals that hold the CDataType pointers */
static void *gl_puppetProps = &dtPuppetProps, *gl_marker = &dtMarker, *gl_enemy = &dtEnemy, *gl_cover = &dtCover,
            *gl_boss1 = &dtBoss1, *gl_boss2 = &dtBoss2, *gl_boss3 = &dtBoss3;

/* objects: vtable at +0, data type pointer and position kept at the end */
typedef struct Obj {
    void **vtbl;
    u8 body[0x400 - 8];
    DT *dt;
    float qv[7];
} Obj;
static void *Obj_GetDataType(void *self) { return ((Obj *)self)->dt; }
static u32 g_placements;
static float *Obj_GetPlacement(void *self, float *out)
{
    g_placements++;
    memcpy(out, ((Obj *)self)->qv, 7 * sizeof(float));
    return out;
}
static void *g_vtbl[0x200 / 8];

static int g_baseInits;
static void Mock_BaseInit(void *ent, void *props) { (void)ent; (void)props; g_baseInits++; }

static Obj *NewObj(DT *dt, float x, float y, float z)
{
    Obj *o = (Obj *)calloc(1, sizeof(Obj));
    o->vtbl = g_vtbl; o->dt = dt;
    o->qv[3] = 1.0f; o->qv[4] = x; o->qv[5] = y; o->qv[6] = z;
    return o;
}
#define U32(o, ofs) (*(u32 *)((u8 *)(o) + (ofs)))
#define PTR(o, ofs) (*(void **)((u8 *)(o) + (ofs)))
static u32 H(void *o) { return Mock_PtrToHandle(o); }
static void Link(Obj *a, Obj *b)   /* a.next = b, b.prev = a */
{
    U32(a, OFS_NEXT_MARKER) = H(b);
    U32(b, OFS_PREV_MARKER) = H(a);
}

/* world */
typedef struct World { u8 pad[0x68]; Obj **ents; i32 count; i32 alloc; } World;
static World g_world;
static Obj *g_ents[4096];
static void WorldAdd(Obj *o) { g_ents[g_world.count++] = o; }
static Obj *Puppet(float x, float y, float z)
{
    Obj *p = NewObj(&dtPuppet, x, y, z);
    PTR(p, OFS_WORLD) = &g_world;
    WorldAdd(p);
    return p;
}
static Obj *Marker(DT *dt, float x, float y, float z) { Obj *m = NewObj(dt, x, y, z); WorldAdd(m); return m; }
static Obj *Props(DT *dt, int autoPatrol) { Obj *p = NewObj(dt, 0, 0, 0); U32(p, MEMBER_OFFSET) = autoPatrol; return p; }

typedef struct Mgr { void *owner; } Mgr;

/* start of a tactic: what CCharacterMoverManager does around the hooked call */
static void *g_cm;   /* resulting en_penCurrentPathMarker */
static void StartTactic(Obj *pup, Obj *tactic)
{
    Mgr m = {pup};
    U32(pup, OFS_CUR_TACTIC) = H(tactic);
    void *tm = AP_TacticMarkerHook(U32(tactic, 0x74), (u8 *)&m);
    if (tm) g_cm = Mock_HandleToPtr(U32(tactic, 0x74));
    else if (Mock_HandleToPtr(U32(pup, OFS_TACTIC_MARKER))) g_cm = Mock_HandleToPtr(U32(pup, OFS_TACTIC_MARKER));
    else g_cm = 0;   /* (engine: unchanged) */
}

static Target g_fakeT;

static void ResetWorld(void)
{
    g_world.ents = g_ents; g_world.count = 0;
}

int main(int argc, char **argv)
{
    g_vtbl[VT_GETDATATYPE / 8] = (void *)Obj_GetDataType;
    g_vtbl[VT_GETPLACEMENT / 8] = (void *)Obj_GetPlacement;
    g_fakeT.dtPuppetProps = (u64)&gl_puppetProps;
    g_fakeT.dtMarker = (u64)&gl_marker;
    g_fakeT.dtEnemyMarker = (u64)&gl_enemy;
    g_fakeT.dtCoverMarker = (u64)&gl_cover;
    g_fakeT.dtBoss1 = (u64)&gl_boss1;
    g_fakeT.dtBoss2 = (u64)&gl_boss2;
    g_fakeT.dtBoss3 = (u64)&gl_boss3;
    g_T = &g_fakeT;
    g_HandleToPtr = Mock_HandleToPtr;
    g_PtrToHandle = Mock_PtrToHandle;
    g_IsDerivedFrom = Mock_IsDerivedFrom;
    g_baseInit = Mock_BaseInit;
    ResetWorld();

    /* --- 1. the init hook remembers the setting -------------------------- */
    Obj *pA = Puppet(0, 0, 0), *pB = Puppet(100, 0, 0);
    Obj *propsOn = Props(&dtLeggedProps, 1), *propsOff = Props(&dtLeggedProps, 0), *propsOther = Props(&dtOtherProps, 1);
    AP_InitHook((u8 *)pA, (u8 *)propsOn);
    AP_InitHook((u8 *)pB, (u8 *)propsOff);
    CHECK(g_baseInits == 2, "base init called");
    CHECK(AP_Get((u8 *)pA) == 1, "A auto patrol");
    CHECK(AP_Get((u8 *)pB) == 0, "B not auto patrol");
    AP_InitHook((u8 *)pB, (u8 *)propsOther);       /* not character properties: ignored */
    CHECK(AP_Get((u8 *)pB) == 0, "B other props");
    AP_InitHook((u8 *)pB, 0);                      /* no properties */
    CHECK(AP_Get((u8 *)pB) == 0 && g_baseInits == 4, "B null props");
    AP_InitHook((u8 *)pA, (u8 *)propsOn);          /* re-initialized (editor): still one entry */
    CHECK(g_used == 1, "one entry, %u", g_used);
    AP_InitHook((u8 *)pA, (u8 *)propsOff);         /* unchecked in the editor */
    CHECK(AP_Get((u8 *)pA) == 0 && g_used == 0, "A removed");
    AP_InitHook((u8 *)pA, (u8 *)propsOn);
    CHECK(AP_Get((u8 *)pA) == 1 && g_used == 1, "A again");

    /* --- 2. nearest marker at the start of the character's own tactic ---- */
    Obj *tac = NewObj(&dtTactic, 0, 0, 0), *tac2 = NewObj(&dtTactic, 0, 0, 0);
    Obj *m1 = Marker(&dtMarker, 10, 0, 0), *m2 = Marker(&dtMarker, 20, 0, 0), *m3 = Marker(&dtMarker, 30, 0, 0);
    Link(m1, m2); Link(m2, m3);
    Obj *far1 = Marker(&dtMarker, -50, 0, 0), *far2 = Marker(&dtMarker, -60, 0, 0);
    Link(far1, far2);
    Obj *tacMarker = Marker(&dtMarker, 500, 0, 0), *tm2 = Marker(&dtMarker, 510, 0, 0);
    Link(tacMarker, tm2);
    U32(tac, 0x74) = H(tacMarker);                 /* the tactic has its own marker */
    U32(pA, OFS_DEF_TACTIC) = H(tac);
    U32(pA, OFS_TACTIC_MARKER) = H(far1);          /* and the character too */
    PTR(pA, 0) = g_vtbl;
    pA->qv[4] = 12; pA->qv[5] = 3; pA->qv[6] = 0;
    StartTactic(pA, tac);
    CHECK(g_cm == m1, "nearest is m1, got %p", g_cm);
    CHECK(Mock_HandleToPtr(U32(pA, OFS_TACTIC_MARKER)) == m1, "tactic marker set to m1");
    CHECK(g_applied == 1, "applied %u", g_applied);

    /* restart of the same tactic later, somewhere else: nearest again */
    pA->qv[4] = 29; pA->qv[5] = 0;
    StartTactic(pA, tac);
    CHECK(g_cm == m3, "restart: m3, got %p", g_cm);

    /* a different tactic (next tactic / script): engine rules */
    U32(tac2, 0x74) = H(tm2);
    StartTactic(pA, tac2);
    CHECK(g_cm == tm2, "other tactic keeps its marker");
    U32(tac2, 0x74) = 0;
    U32(pA, OFS_TACTIC_MARKER) = H(far2);
    StartTactic(pA, tac2);
    CHECK(g_cm == far2, "other tactic without marker: character's marker");

    /* the character without Auto patrol: engine rules */
    U32(pB, OFS_DEF_TACTIC) = H(tac);
    U32(pB, OFS_TACTIC_MARKER) = H(far1);
    pB->qv[4] = 11;
    StartTactic(pB, tac);
    CHECK(g_cm == tacMarker, "B: tactic's own marker");
    CHECK(Mock_HandleToPtr(U32(pB, OFS_TACTIC_MARKER)) == far1, "B: marker unchanged");
    U32(tac, 0x74) = 0;
    StartTactic(pB, tac);
    CHECK(g_cm == far1, "B: character's marker");

    /* no default tactic: engine rules */
    U32(pA, OFS_DEF_TACTIC) = 0;
    U32(pA, OFS_TACTIC_MARKER) = H(far2);
    StartTactic(pA, tac);
    CHECK(g_cm == far2, "no default tactic");
    U32(pA, OFS_DEF_TACTIC) = H(tac);

    /* --- 3. which markers count ------------------------------------------ */
    ResetWorld();
    WorldAdd(pA);
    pA->qv[4] = 0; pA->qv[5] = 0; pA->qv[6] = 0;
    Obj *lone = Marker(&dtMarker, 1, 0, 0);                 /* not part of a path */
    Obj *boss = Marker(&dtBoss2, 1.5f, 0, 0), *bossN = Marker(&dtBoss2, 1.6f, 0, 0);
    Link(boss, bossN);
    Obj *light = Marker(&dtLight, 0.5f, 0, 0);              /* not a marker at all */
    Obj *multi = Marker(&dtMulti, 3, 0, 0);                 /* plugin marker, links unknown */
    Obj *en1 = Marker(&dtEnemy, 2, 0, 0), *en2 = Marker(&dtEnemy, 9, 0, 0);
    Link(en1, en2);
    (void)lone; (void)light;
    StartTactic(pA, tac);
    CHECK(g_cm == en1, "enemy marker in a path, got %p", g_cm);
    Mock_Release(en1);                                      /* deleted: its link is stale */
    U32(en2, OFS_PREV_MARKER) = 0;
    g_world.count = 0; WorldAdd(pA); WorldAdd(lone); WorldAdd(boss); WorldAdd(bossN); WorldAdd(light); WorldAdd(multi); WorldAdd(en2);
    StartTactic(pA, tac);
    CHECK(g_cm == multi, "multipath marker without links, got %p", g_cm);
    Obj *cov = Marker(&dtCover, 2.5f, 0, 0), *cov2 = Marker(&dtCover, 2.6f, 0, 0);
    StartTactic(pA, tac);
    CHECK(g_cm == multi, "lone cover markers skipped");
    Link(cov, cov2);
    StartTactic(pA, tac);
    CHECK(g_cm == cov, "cover marker in a path");

    /* --- 4. the character's own intro and outro paths are skipped -------- */
    ResetWorld();
    WorldAdd(pA);
    Obj *i1 = Marker(&dtMarker, -100, 0, 0), *i2 = Marker(&dtMarker, -50, 0, 0), *i3 = Marker(&dtMarker, 0, 0, 1);
    Link(i1, i2); Link(i2, i3);
    Obj *o1 = Marker(&dtMarker, 0.9f, 0, 0), *o2 = Marker(&dtMarker, 200, 0, 0);
    Link(o1, o2);
    Obj *p1 = Marker(&dtMarker, 5, 0, 0), *p2 = Marker(&dtMarker, 6, 0, 0), *p3 = Marker(&dtMarker, 7, 0, 0);
    Link(p1, p2); Link(p2, p3); Link(p3, p1);               /* a loop */
    U32(pA, OFS_INTRO_MARKER) = H(i1);
    U32(pA, OFS_OUTRO_MARKER) = H(o1);
    StartTactic(pA, tac);                                   /* standing at the end of the intro path */
    CHECK(g_cm == p1, "intro/outro skipped, got %p", g_cm);
    U32(pA, OFS_INTRO_MARKER) = H(p2);                      /* an intro "path" that is a loop: terminates */
    U32(pA, OFS_OUTRO_MARKER) = 0;
    StartTactic(pA, tac);
    CHECK(g_cm == o1, "loop as intro, got %p", g_cm);
    U32(pA, OFS_INTRO_MARKER) = 0;
    U32(pA, OFS_OUTRO_MARKER) = H(o1);
    StartTactic(pA, tac);
    CHECK(g_cm == i3, "outro only: i3, got %p", g_cm);
    U32(pA, OFS_OUTRO_MARKER) = 0;
    StartTactic(pA, tac);
    CHECK(g_cm == o1, "no intro/outro: o1, got %p", g_cm);

    /* nothing found: engine rules */
    ResetWorld();
    WorldAdd(pA);
    U32(tac, 0x74) = H(tacMarker);
    StartTactic(pA, tac);
    CHECK(g_cm == tacMarker, "no marker in the world");

    /* character without a world: engine rules */
    PTR(pA, OFS_WORLD) = 0;
    StartTactic(pA, tac);
    CHECK(g_cm == tacMarker, "no world");
    PTR(pA, OFS_WORLD) = &g_world;

    /* --- 5. deleted characters ------------------------------------------- */
    CHECK(AP_Get((u8 *)pA) == 1, "A present");
    Mock_Release(pA);
    CHECK(AP_Get((u8 *)pA) == 0, "A deleted -> not auto patrol");
    CHECK(g_used == 0, "entry dropped, used %u", g_used);
    /* a new character at the same address gets a new handle */
    AP_InitHook((u8 *)pA, (u8 *)propsOff);
    CHECK(AP_Get((u8 *)pA) == 0, "reused address, unchecked");

    /* --- 6. many characters ---------------------------------------------- */
    {
        enum { N = 7000 };
        static Obj *many[N];
        for (int i = 0; i < N; i++) { many[i] = NewObj(&dtPuppet, (float)i, 0, 0); AP_InitHook((u8 *)many[i], (u8 *)propsOn); }
        u32 present = 0;
        for (int i = 0; i < N; i++) present += AP_Get((u8 *)many[i]);
        CHECK(present == AP_TAB * 3 / 4 && g_full, "table limit: %u present, full %u", present, g_full);
        for (int i = 0; i < N; i++) if (i % 2) Mock_Release(many[i]);   /* half of them deleted */
        g_full = 0;
        Obj *late = NewObj(&dtPuppet, 0, 0, 0);
        AP_InitHook((u8 *)late, (u8 *)propsOn);                         /* forces a rebuild */
        CHECK(AP_Get((u8 *)late) == 1 && !g_full, "inserted after rebuild");
        present = 0;
        for (int i = 0; i < N; i++) present += AP_Get((u8 *)many[i]);
        u32 expect = 0;
        for (int i = 0; i < AP_TAB * 3 / 4 && i < N; i++) if (!(i % 2)) expect++;
        CHECK(present == expect, "after rebuild %u of %u", present, expect);
        for (int i = 0; i < N; i++) if (!(i % 2)) AP_InitHook((u8 *)many[i], (u8 *)propsOff);
        CHECK(g_used == 1, "only late left: %u", g_used);
        for (int i = 0; i < 20000; i++) {   /* churn: insert/remove, tombstones */
            Obj *t = many[(i * 7) % N];
            if (i % 3 == 0) Mock_Release(t);
            AP_InitHook((u8 *)t, (u8 *)((i & 1) ? propsOn : propsOff));
        }
        CHECK(g_used + g_deleted <= AP_TAB, "consistent counts");
        u32 cnt = 0, del = 0;
        for (u32 i = 0; i < AP_TAB; i++) { if (g_tab[i].state == 1) cnt++; if (g_tab[i].state == 2) del++; }
        CHECK(cnt == g_used && del == g_deleted, "counts match table %u/%u %u/%u", cnt, g_used, del, g_deleted);
    }

    /* --- 7. type cache --------------------------------------------------- */
    {
        u32 before = g_derivedCalls;
        for (int i = 0; i < 100; i++) (void)MarkerKind(&dtMulti);
        CHECK(g_derivedCalls == before, "cached");
        CHECK(MarkerKind(&dtMarker) == 1 && MarkerKind(&dtEnemy) == 1 && MarkerKind(&dtCover) == 1, "engine kinds");
        CHECK(MarkerKind(&dtMulti) == 2 && MarkerKind(&dtBoss1) == 0 && MarkerKind(&dtBoss3) == 0 && MarkerKind(&dtLight) == 0, "kinds");
    }

    /* --- 8. meta data stream --------------------------------------------- */
    if (argc >= 3) {
        static u64 src[STREAM_QWORDS];
        FILE *in = fopen(argv[1], "rb");
        if (!in || fread(src, 8, STREAM_QWORDS, in) != STREAM_QWORDS) { printf("cannot read %s\n", argv[1]); return 2; }
        fclose(in);
        u32 nq = BuildStream(src);
        CHECK(nq == NEW_QWORDS, "stream qwords %u", nq);
        CHECK(g_stream[IDX_FORMAT] == NEW_FORMAT && g_stream[IDX_NMEMBERS] == OLD_NMEMBERS + 1, "header");
        CHECK(memcmp(g_stream + IDX_INSERT + MEMBER_QWORDS, src + IDX_INSERT, (STREAM_QWORDS - IDX_INSERT) * 8) == 0, "tail");
        FILE *out = fopen(argv[2], "wb");
        fwrite(g_stream, 8, nq, out);
        const char *strs[3] = {s_memDisp, s_memName, s_memId};
        u32 ns = 3;
        fwrite(&ns, 4, 1, out);
        for (u32 i = 0; i < ns; i++) {
            u64 a = (u64)strs[i]; u32 l = (u32)strlen(strs[i]);
            fwrite(&a, 8, 1, out); fwrite(&l, 4, 1, out); fwrite(strs[i], 1, l, out);
        }
        fclose(out);
        printf("stream written: %u qwords\n", nq);
    }

    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
