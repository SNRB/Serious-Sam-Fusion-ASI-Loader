/* Native (Linux) test of the plugin's selection logic and meta data stream
 * builder, using mock engine objects. Build: see run_tests.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mso_logic.h"

/* ---- mock engine ------------------------------------------------------- */
static void *g_handles[16384];
static u32 g_nhandles = 1;
static void *Mock_HandleToPtr(u32 h) { return (h && h < g_nhandles) ? g_handles[h] : 0; }
static u32 Mock_PtrToHandle(void *p)
{
    if (!p) return 0;
    for (u32 i = 1; i < g_nhandles; i++) if (g_handles[i] == p) return i;
    g_handles[g_nhandles] = p;
    return g_nhandles++;
}
static int g_warns;
static void Mock_Warn(const char *fmt, ...) { (void)fmt; g_warns++; }
static int g_origCalls;
static u32 *Mock_OrigGetTemplate(void *self, u32 *out, i32 iSlot, i32 ctMax, u32 ulDiff)
{
    (void)self; (void)iSlot; (void)ctMax; (void)ulDiff;
    g_origCalls++;
    *out = 0xDEAD;
    return out;
}

/* template object: vtable[0x138/8] = IsMultiSpawnerTemplate */
typedef struct Tpl { void **vtbl; i32 isMST; const char *name; } Tpl;
static i32 Tpl_IsMST(void *self) { return ((Tpl *)self)->isMST; }
static void *g_tplVtbl[64];

static Target g_fakeT;
#define FAKE_MSE_VT 0x1111000011110000ULL
#define FAKE_SPW_VT 0x2222000022220000ULL

typedef struct Fixture {
    Tpl tpl[16];
    WST wst[16];
    WST *ptrs[16];
    Container cont;
    u8 props[0x98];
    u8 mse[0x60];
    u8 spw[0x150];
} Fixture;

static void Setup(Fixture *f, const char **names, const int *weights, const u32 *diff, const int *mst, int n)
{
    memset(f, 0, sizeof(*f));
    for (int i = 0; i < n; i++) {
        f->tpl[i].vtbl = g_tplVtbl;
        f->tpl[i].isMST = mst ? mst[i] : 1;
        f->tpl[i].name = names[i];
        f->wst[i].pepTemplateCopy = &f->tpl[i];
        f->wst[i].ctSpawnWeight = weights[i];
        f->wst[i].ulDifficulty = diff ? diff[i] : 0xffffffff;
        f->ptrs[i] = &f->wst[i];
    }
    f->cont.data = f->ptrs;
    f->cont.count = n;
    *(u32 *)(f->props + 0x88) = Mock_PtrToHandle(&f->cont);
    *(u8 **)(f->props + 0x08) = f->mse;
    *(u64 *)f->mse = FAKE_MSE_VT;
    *(u32 *)(f->mse + 0x5c) = Mock_PtrToHandle(f->spw);
    *(u64 *)f->spw = FAKE_SPW_VT;
}

static const char *Pick(Fixture *f, int mode, int slot, int ctMax, u32 diff)
{
    *(i32 *)(f->props + 0x94) = mode;
    u32 h = 0;
    MSO_GetTemplate(f->props, &h, slot, ctMax, diff);
    if (h == 0xDEAD) return "<orig>";
    Tpl *t = (Tpl *)Mock_HandleToPtr(h);
    return t ? t->name : "<null>";
}

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void SeqStr(Fixture *f, int mode, int n, int ctMax, char *buf)
{
    buf[0] = 0;
    for (int s = 0; s < n; s++) {
        const char *p = Pick(f, mode, s, ctMax, 0xffffffff);
        strcat(buf, p[0] == '<' ? "?" : (char[]){p[0], 0});
    }
}

/* each cycle of 'cyc' slots contains exactly weight[i] of template i */
static int CycleCountsOK(const char *seq, int n, int cyc, const char *letters, const int *w, int nt)
{
    for (int start = 0; start + cyc <= n; start += cyc)
        for (int t = 0; t < nt; t++) {
            int c = 0;
            for (int k = 0; k < cyc; k++) if (seq[start + k] == letters[t]) c++;
            if (c != w[t]) return 0;
        }
    return 1;
}

/* each template's slots inside a cycle are contiguous */
static int GroupedOK(const char *seq, int n, int cyc)
{
    for (int start = 0; start + cyc <= n; start += cyc) {
        char seen[256] = {0};
        for (int k = 0; k < cyc; k++) {
            unsigned char ch = (unsigned char)seq[start + k];
            if (k && seq[start + k - 1] != ch && seen[ch]) return 0;
            seen[ch] = 1;
        }
    }
    return 1;
}


/* ---- large tables (more usable rows than the 512 of version 1.0) -------- */
typedef struct BigFix { Tpl *tpl; WST *wst; WST **ptr; Container cont; u8 props[0x98]; u8 mse[0x60]; u8 spw[0x150]; } BigFix;

static BigFix *BigSetup(int n, const int *weights)
{
    BigFix *b = (BigFix *)calloc(1, sizeof(BigFix));
    b->tpl = (Tpl *)calloc(n, sizeof(Tpl));
    b->wst = (WST *)calloc(n, sizeof(WST));
    b->ptr = (WST **)calloc(n, sizeof(WST *));
    for (int i = 0; i < n; i++) {
        b->tpl[i].vtbl = g_tplVtbl; b->tpl[i].isMST = 1; b->tpl[i].name = "x";
        b->wst[i].pepTemplateCopy = &b->tpl[i];
        b->wst[i].ctSpawnWeight = weights ? weights[i] : 1;
        b->wst[i].ulDifficulty = 0xffffffff;
        b->ptr[i] = &b->wst[i];
    }
    b->cont.data = b->ptr; b->cont.count = n;
    *(u32 *)(b->props + 0x88) = Mock_PtrToHandle(&b->cont);
    *(u8 **)(b->props + 0x08) = b->mse;
    *(u64 *)b->mse = FAKE_MSE_VT;
    *(u32 *)(b->mse + 0x5c) = Mock_PtrToHandle(b->spw);
    *(u64 *)b->spw = FAKE_SPW_VT;
    *(u32 *)(b->props + 0x90) = 0x3e99999au;
    return b;
}

/* index of the template picked for a slot, -1 if none */
static int BigPick(BigFix *b, int mode, int slot, int ctMax)
{
    *(i32 *)(b->props + 0x94) = mode;
    u32 h = 0;
    MSO_GetTemplate(b->props, &h, slot, ctMax, 0xffffffff);
    Tpl *t = (Tpl *)Mock_HandleToPtr(h);
    return t ? (int)(t - b->tpl) : -1;
}

/* every cycle holds each template exactly weight times; for groups also contiguous */
static int BigCheck(BigFix *b, int n, const int *weights, int mode, int cycles, int *outFirst)
{
    i64 W = 0;
    for (int i = 0; i < n; i++) W += weights ? weights[i] : 1;
    int *cnt = (int *)calloc(n, sizeof(int)), *seq = (int *)calloc((size_t)W, sizeof(int));
    int ok = 1;
    for (int cy = 0; cy < cycles && ok; cy++) {
        memset(cnt, 0, n * sizeof(int));
        for (i64 r = 0; r < W; r++) {
            int t = BigPick(b, mode, (int)(cy * W + r), 1 << 30);
            seq[r] = t;
            if (t < 0 || t >= n) { ok = 0; break; }
            cnt[t]++;
        }
        for (int i = 0; ok && i < n; i++) if (cnt[i] != (weights ? weights[i] : 1)) ok = 0;
        if (ok && mode != TOM_FULL_RANDOM)          /* each template's block is contiguous */
            for (i64 r = 1; r < W; r++)
                if (seq[r] != seq[r - 1]) { cnt[seq[r - 1]] = -1; if (cnt[seq[r]] == -1) { ok = 0; break; } }
        if (cy == 0 && outFirst) *outFirst = seq[0];
    }
    free(cnt); free(seq);
    return ok;
}

int main(int argc, char **argv)
{
    g_tplVtbl[0x138 / 8] = (void *)Tpl_IsMST;
    g_fakeT.mseVtable = FAKE_MSE_VT;
    g_fakeT.spawnerVtable = FAKE_SPW_VT;
    g_fakeT.WarnStr = (u64)"warning";
    g_T = &g_fakeT;
    g_HandleToPtr = Mock_HandleToPtr;
    g_PtrToHandle = Mock_PtrToHandle;
    g_Warn = Mock_Warn;
    g_origGetTemplate = Mock_OrigGetTemplate;
    g_rng = 12345;
    g_fallbackSeed = 777;

    static Fixture F;
    const char *names[] = {"Kleer", "Gnaar", "Biomech"};
    const int w[] = {3, 2, 1};
    char seq[512];

    /* --- Default and invalid values call the original ------------------ */
    Setup(&F, names, w, 0, 0, 3);
    g_origCalls = 0;
    CHECK(!strcmp(Pick(&F, TOM_DEFAULT, 0, 6, ~0u), "<orig>"), "default -> original");
    CHECK(!strcmp(Pick(&F, 4, 0, 6, ~0u), "<orig>"), "4 -> original");
    CHECK(!strcmp(Pick(&F, -1, 0, 6, ~0u), "<orig>"), "-1 -> original");
    CHECK(!strcmp(Pick(&F, 0x7fffffff, 0, 6, ~0u), "<orig>"), "big -> original");
    CHECK(g_origCalls == 4, "orig calls %d", g_origCalls);

    /* --- Groups (static seed) ------------------------------------------ */
    printf("Groups, Kleer-3 Gnaar-2 Biomech-1, 18 spawns, total count 18:\n");
    int perms[6] = {0};
    for (u32 rb = 0; rb < 600; rb++) {
        Setup(&F, names, w, 0, 0, 3);
        *(u32 *)(F.props + 0x90) = 0x3f000000u + rb * 2654435761u % 0x800000u; /* some floats */
        SeqStr(&F, TOM_GROUPS, 18, 18, seq);
        CHECK(CycleCountsOK(seq, 18, 6, "KGB", w, 3), "groups counts %s", seq);
        CHECK(GroupedOK(seq, 18, 6), "groups contiguous %s", seq);
        CHECK(!memcmp(seq, seq + 6, 6) && !memcmp(seq, seq + 12, 6), "groups static per cycle %s", seq);
        char again[64];
        F.spw[SEED_OFFSET] = 0x55;   /* a run seed must not matter for Groups */
        SeqStr(&F, TOM_GROUPS, 18, 18, again);
        CHECK(!strcmp(seq, again), "groups is static %s %s", seq, again);
        if (rb < 6) printf("  random base #%u: %s\n", rb, seq);
        /* which of the 6 group orders */
        const char *orders[6] = {"KKKGGB", "KKKBGG", "GGKKKB", "GGBKKK", "BKKKGG", "BGGKKK"};
        for (int k = 0; k < 6; k++) if (!memcmp(seq, orders[k], 6)) perms[k]++;
    }
    printf("  distribution of the 6 group orders over 600 random bases: %d %d %d %d %d %d\n",
           perms[0], perms[1], perms[2], perms[3], perms[4], perms[5]);
    for (int k = 0; k < 6; k++) CHECK(perms[k] > 60, "order %d rare (%d)", k, perms[k]);

    /* --- Full random ----------------------------------------------------- */
    printf("Full random, 18 spawns, total count 18, 4 runs:\n");
    char runs[4][64];
    for (int run = 0; run < 4; run++) {
        Setup(&F, names, w, 0, 0, 3);          /* new CSpawnerEntity -> seed 0 */
        *(u32 *)(F.props + 0x90) = 0x3f123456u;
        SeqStr(&F, TOM_FULL_RANDOM, 18, 18, runs[run]);
        printf("  run %d: %s  (seed %08x)\n", run, runs[run], *(u32 *)(F.spw + SEED_OFFSET));
        CHECK(*(u32 *)(F.spw + SEED_OFFSET) != 0, "seed stored");
        CHECK(CycleCountsOK(runs[run], 18, 6, "KGB", w, 3), "full random counts %s", runs[run]);
        char again[64];
        SeqStr(&F, TOM_FULL_RANDOM, 18, 18, again);   /* same run, same seed */
        CHECK(!strcmp(runs[run], again), "full random stable within a run");
    }
    CHECK(strcmp(runs[0], runs[1]) || strcmp(runs[1], runs[2]) || strcmp(runs[2], runs[3]), "runs differ");

    /* --- Groups full random ------------------------------------------------ */
    printf("Groups full random, 18 spawns, total count 18, 4 runs:\n");
    for (int run = 0; run < 4; run++) {
        Setup(&F, names, w, 0, 0, 3);
        *(u32 *)(F.props + 0x90) = 0x3f123456u;
        SeqStr(&F, TOM_GROUPS_FULL_RANDOM, 18, 18, runs[run]);
        printf("  run %d: %s\n", run, runs[run]);
        CHECK(CycleCountsOK(runs[run], 18, 6, "KGB", w, 3), "gfr counts %s", runs[run]);
        CHECK(GroupedOK(runs[run], 18, 6), "gfr contiguous %s", runs[run]);
    }

    /* --- total count smaller than the weights (same rule as the original) */
    Setup(&F, names, w, 0, 0, 3);
    *(u32 *)(F.props + 0x90) = 0x3e800000u;
    SeqStr(&F, TOM_GROUPS, 4, 4, seq);
    printf("Groups with total count 4: %s\n", seq);
    CHECK(strlen(seq) == 4 && !strchr(seq, '?'), "truncated groups");
    SeqStr(&F, TOM_FULL_RANDOM, 8, 4, seq);
    printf("Full random with total count 4 (8 slots): %s\n", seq);

    /* --- difficulty filter, not-a-template warning, zero weight ---------- */
    {
        const char *n5[] = {"Kleer", "Gnaar", "Biomech", "Werebull", "Zombie"};
        const int w5[] = {3, 2, 1, 4, 0};
        const u32 d5[] = {0x8, 0x8, 0x8, 0x10, 0x8};
        const int m5[] = {1, 1, 0, 1, 1};  /* Biomech not marked as multi spawner template */
        Setup(&F, n5, w5, d5, m5, 5);
        g_warns = 0;
        *(i32 *)(F.props + 0x94) = TOM_GROUPS;
        char s2[64] = {0};
        for (int s = 0; s < 10; s++) {
            u32 h = 0;
            MSO_GetTemplate(F.props, &h, s, 100, 0x8 /* normal */);
            Tpl *t = (Tpl *)Mock_HandleToPtr(h);
            s2[s] = t ? t->name[0] : '?';
        }
        printf("difficulty 0x8, Biomech not a template, Zombie weight 0: %s (warnings %d)\n", s2, g_warns);
        CHECK(!strchr(s2, 'W') && !strchr(s2, 'B') && !strchr(s2, 'Z') && !strchr(s2, '?'), "filter %s", s2);
        CHECK(g_warns == 10, "one warning per call (%d)", g_warns);
        CHECK(CycleCountsOK(s2, 10, 5, "KG", w5, 2), "filter counts %s", s2);
    }

    /* --- no spawner entity (e.g. editor preview): fallback seed ---------- */
    Setup(&F, names, w, 0, 0, 3);
    *(u8 **)(F.props + 0x08) = 0;
    SeqStr(&F, TOM_FULL_RANDOM, 6, 6, seq);
    CHECK(CycleCountsOK(seq, 6, 6, "KGB", w, 3), "fallback %s", seq);
    /* wrong vtable: must not touch the object */
    Setup(&F, names, w, 0, 0, 3);
    *(u64 *)F.spw = 0x1234;
    SeqStr(&F, TOM_FULL_RANDOM, 6, 6, seq);
    CHECK(*(u32 *)(F.spw + SEED_OFFSET) == 0, "seed written into a non-spawner");

    /* --- empty / null provider ------------------------------------------- */
    Setup(&F, names, w, 0, 0, 0);
    CHECK(!strcmp(Pick(&F, TOM_GROUPS, 0, 6, ~0u), "<null>"), "empty provider");
    *(u32 *)(F.props + 0x88) = 0;
    CHECK(!strcmp(Pick(&F, TOM_FULL_RANDOM, 0, 6, ~0u), "<null>"), "null provider");

    /* --- big weights (Feistel path) -------------------------------------- */
    {
        const char *nb[] = {"Kleer", "Gnaar"};
        const int wb[] = {700, 600};
        Setup(&F, nb, wb, 0, 0, 2);
        int ck = 0, cg = 0;
        static char big[1400];
        for (int s = 0; s < 1300; s++) {
            const char *p = Pick(&F, TOM_FULL_RANDOM, s, 100000, ~0u);
            big[s] = p[0];
            if (p[0] == 'K') ck++; else if (p[0] == 'G') cg++;
        }
        CHECK(ck == 700 && cg == 600, "feistel counts %d %d", ck, cg);
        int runsK = 0;  /* not sorted: there must be switches */
        for (int s = 1; s < 1300; s++) if (big[s] != big[s - 1]) runsK++;
        CHECK(runsK > 100, "feistel looks random (%d switches)", runsK);
    }


    /* --- large tables: 1356 rows (the reported case), and the 512/513 edge */
    {
        const int sizes[] = {1356, 512, 513, 2000};
        for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
            int n = sizes[k];
            int *wv = (int *)malloc(n * sizeof(int));
            for (int i = 0; i < n; i++) wv[i] = 1 + (i * 7) % 3;
            for (int pass = 0; pass < 2; pass++) {
                const int *weights = pass ? wv : 0;
                BigFix *b = BigSetup(n, weights);
                int f1 = -1, f2 = -2;
                CHECK(BigCheck(b, n, weights, TOM_GROUPS, 2, &f1), "big groups n=%d w=%d", n, pass);
                CHECK(BigCheck(b, n, weights, TOM_FULL_RANDOM, 2, 0), "big full random n=%d w=%d", n, pass);
                CHECK(BigCheck(b, n, weights, TOM_GROUPS_FULL_RANDOM, 2, &f2), "big groups full random n=%d w=%d", n, pass);
                /* the shuffle must reach the last rows too, not only the first ones */
                int late = 0;
                for (int s = 0; s < 64; s++) if (BigPick(b, TOM_GROUPS, s * (n / 64), 1 << 30) >= n / 2) late++;
                CHECK(late > 8, "groups uses the whole table n=%d (%d of 64 from the second half)", n, late);
                late = 0;
                for (int s = 0; s < 64; s++) if (BigPick(b, TOM_FULL_RANDOM, s, 1 << 30) >= n / 2) late++;
                CHECK(late > 8, "full random uses the whole table n=%d (%d of 64)", n, late);
            }
            free(wv);
        }
        printf("large tables checked (1356, 512, 513, 2000 rows)\n");
    }

    /* --- the permutation helpers are bijections --------------------------- */
    for (u32 n = 1; n < 1200; n += (n < 40 ? 1 : 37)) {
        static u8 hit[70000];
        memset(hit, 0, n);
        for (u32 i = 0; i < n; i++) {
            u32 p = PermIndex(i, n, 0xABCDEF12345ULL + n);
            CHECK(p < n && !hit[p], "perm n=%u i=%u p=%u", n, i, p);
            if (p < n) hit[p] = 1;
        }
    }

    /* --- meta data stream ------------------------------------------------- */
    if (argc >= 3) {
        FILE *in = fopen(argv[1], "rb");
        u64 src[STREAM_QWORDS];
        if (!in || fread(src, 8, STREAM_QWORDS, in) != STREAM_QWORDS) { printf("cannot read %s\n", argv[1]); return 2; }
        fclose(in);
        u32 nq = BuildStream(src);
        CHECK(nq == NEW_QWORDS, "stream qwords %u", nq);
        FILE *out = fopen(argv[2], "wb");
        fwrite(g_stream, 8, nq, out);
        /* the addresses of our strings, for the decoder */
        const char *strs[] = {s_enumName, s_v0, s_d0, s_v1, s_d1, s_v2, s_d2, s_v3, s_d3, s_memDisp, s_memName, s_memId};
        u32 ns = sizeof(strs) / sizeof(strs[0]);
        fwrite(&ns, 4, 1, out);
        for (u32 i = 0; i < ns; i++) {
            u64 a = (u64)strs[i];
            u32 l = (u32)strlen(strs[i]);
            fwrite(&a, 8, 1, out); fwrite(&l, 4, 1, out); fwrite(strs[i], 1, l, out);
        }
        u64 ge = (u64)&g_enumType;
        fwrite(&ge, 8, 1, out);
        fclose(out);
        printf("stream written: %u qwords\n", nq);
    }

    printf(g_fail ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", g_fail);
    return g_fail != 0;
}
