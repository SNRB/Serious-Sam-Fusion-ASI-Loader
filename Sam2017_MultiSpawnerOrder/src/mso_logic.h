/* mso_logic.h - template selection and meta data stream builder
 * (pure code: used by the plugin and by the native test program) */
#ifndef MSO_LOGIC_H
#define MSO_LOGIC_H
#include "mso_common.h"

/* ------------------------------------------------------------------------- */
/* runtime: template selection                                               */
/* ------------------------------------------------------------------------- */
enum { TOM_DEFAULT = 0, TOM_GROUPS = 1, TOM_FULL_RANDOM = 2, TOM_GROUPS_FULL_RANDOM = 3 };

typedef u32  *(*GetTemplate_t)(void *self, u32 *out, i32 iSlot, i32 ctMax, u32 ulDiff);
typedef void *(*HandleToPtr_t)(u32 h);
typedef u32   (*PtrToHandle_t)(void *p);
typedef void  (*Warn_t)(const char *fmt, ...);
typedef i32   (*VFuncBool_t)(void *self);

/* CWeightedSpawnTemplate (0x18 bytes) */
typedef struct WST {
    u32   hTemplate;        /* wst_hepTemplate          */
    u32   _pad;
    void *pepTemplateCopy;  /* wst_pepTemplateCopy      */
    i32   ctSpawnWeight;    /* wst_ctSpawnWeight        */
    u32   ulDifficulty;     /* wst_ulDifficultyFlags    */
} WST;

/* CSpawnTemplateProvider = CDynamicContainer<CWeightedSpawnTemplate> */
typedef struct Container {
    WST **data;
    i32   count;
} Container;

static const Target *g_T;
static GetTemplate_t g_origGetTemplate;
static HandleToPtr_t g_HandleToPtr;
static PtrToHandle_t g_PtrToHandle;
static Warn_t        g_Warn;
static u64           g_rng;          /* per-process random state */
static u64           g_fallbackSeed; /* when no running spawner can be found */

static u64 Mix64(u64 z)
{
    z ^= z >> 33; z *= 0xff51afd7ed558ccdULL;
    z ^= z >> 33; z *= 0xc4ceb9fe1a85ec53ULL;
    z ^= z >> 33;
    return z;
}

static u64 SplitMix(u64 *s)
{
    u64 z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static u32 RandBelow(u64 *s, u32 n)    /* [0, n) */
{
    return (u32)(((SplitMix(s) >> 32) * (u64)n) >> 32);
}

#define MAXPERM 512

/* Fisher-Yates shuffle of a[0..n), fully determined by key */
static void Shuffle16(u16 *a, u32 n, u64 key)
{
    u64 s = key;
    for (u32 i = n; i > 1; i--) {
        u32 j = RandBelow(&s, i);
        u16 t = a[i - 1]; a[i - 1] = a[j]; a[j] = t;
    }
}

/* permutation of [0, n) for large n (no memory): Feistel network + cycle walking */
static u32 FeistelPerm(u32 i, u32 n, u64 key)
{
    u32 bits = 2;
    while (bits < 32 && ((u64)1 << bits) < (u64)n) bits += 2;
    u32 half = bits / 2, mask = (u32)(((u64)1 << half) - 1);
    u32 x = i;
    do {
        u32 L = x >> half, R = x & mask;
        for (u32 r = 0; r < 6; r++) {
            u32 F = (u32)Mix64(key ^ ((u64)r << 56) ^ R) & mask;
            u32 t = R; R = L ^ F; L = t;
        }
        x = (L << half) | R;
    } while (x >= n);
    return x;
}

/* element i of a random permutation of [0, n) */
__attribute__((noinline)) static u32 PermIndex(u32 i, u32 n, u64 key)
{
    if (n <= 1) return 0;
    if (n <= MAXPERM) {
        u16 a[MAXPERM];
        for (u32 k = 0; k < n; k++) a[k] = (u16)k;
        Shuffle16(a, n, key);
        return a[i];
    }
    return FeistelPerm(i, n, key);
}

static u32 NewSeed(void)
{
    u64 v = SplitMix(&g_rng) ^ Mix64(__builtin_ia32_rdtsc());
    u32 s = (u32)(v ^ (v >> 32));
    return s ? s : 0x9E3779B9u;
}

/* Seed of the current run: kept in the CSpawnerEntity that does the spawning
   (a new entity - and so a new seed - is created on every level start /
   editor simulation). */
static u64 RunSeed(u8 *self)
{
    u8 *ent = *(u8 **)(self + 0x08);                       /* ep_penEntity   */
    if (ent && *(u64 *)ent == g_T->mseVtable) {            /* CMultiSpawnerEntity */
        u8 *spw = (u8 *)g_HandleToPtr(*(u32 *)(ent + 0x5c)); /* en_henSpawner */
        if (spw && *(u64 *)spw == g_T->spawnerVtable) {    /* CSpawnerEntity */
            volatile u32 *slot = (volatile u32 *)(spw + SEED_OFFSET);
            u32 s = *slot;
            if (!s) { s = NewSeed(); *slot = s; }
            return s;
        }
    }
    return g_fallbackSeed;
}

static i32 IsMultiSpawnerTemplate(void *pep)
{
    VFuncBool_t f = (*(VFuncBool_t **)pep)[0x138 / 8];
    return f(pep);
}

/* the filter of the first pass, without its warning (for the second pass) */
static i32 IsUsable(WST *w, u32 ulDiff)
{
    return w && w->pepTemplateCopy && (w->ulDifficulty & ulDiff) && w->ctSpawnWeight > 0 &&
           IsMultiSpawnerTemplate(w->pepTemplateCopy);
}

/* the same Fisher-Yates shuffle as Shuffle16, on 32-bit entries */
static void Shuffle32(u32 *a, u32 n, u64 key)
{
    u64 s = key;
    for (u32 i = n; i > 1; i--) {
        u32 j = RandBelow(&s, i);
        u32 t = a[i - 1]; a[i - 1] = a[j]; a[j] = t;
    }
}

/* CMultiSpawnerProperties::GetTemplate(Handle *out, INDEX iSlot, INDEX ctMax, ULONG ulDiff) */
static u32 *MSO_GetTemplate(u8 *self, u32 *out, i32 iSlot, i32 ctMax, u32 ulDiff)
{
    i32 mode = *(i32 *)(self + MEMBER_OFFSET);
    if (mode < TOM_GROUPS || mode > TOM_GROUPS_FULL_RANDOM)
        return g_origGetTemplate(self, out, iSlot, ctMax, ulDiff);

    *out = 0;   /* = PtrToHandle(NULL) */
    Container *c = (Container *)g_HandleToPtr(*(u32 *)(self + 0x88)); /* ep_hstpTemplateProvider */
    if (!c) return out;

    /* usable templates, in table order (same filter as the original). All of
       them count; the first MAXPERM are also listed in v[] (version 1.0
       stopped at MAXPERM, so rows further down a long table never spawned). */
    u32 v[MAXPERM];
    u32 K = 0;
    i64 W = 0;
    for (i32 i = 0; i < c->count; i++) {
        WST *w = c->data[i];
        if (!w || !w->pepTemplateCopy || !(w->ulDifficulty & ulDiff)) continue;
        if (!IsMultiSpawnerTemplate(w->pepTemplateCopy)) {
            g_Warn((const char *)g_T->WarnStr);
            continue;
        }
        if (w->ctSpawnWeight <= 0) continue;
        if (K < MAXPERM) v[K] = (u32)i;
        K++;
        W += w->ctSpawnWeight;
    }
    if (!K) return out;
    i32 listed = K <= MAXPERM;   /* all usable templates are in v[] */

    /* same cycle length as the original: min(total weight, total count), at least 1 */
    i64 N64 = W < (i64)ctMax ? W : (i64)ctMax;
    if (N64 < 1) N64 = 1;
    i32 N = (i32)N64;
    i32 r = iSlot % N, cycle = iSlot / N;
    if (r < 0) { r += N; cycle--; }

    u64 key;
    if (mode == TOM_GROUPS)  /* static: the spawner's random base, like the original */
        key = Mix64(0x6A09E667F3BCC908ULL ^ (u64)*(u32 *)(self + 0x90)); /* ep_fRndBase bits */
    else                     /* per run, reshuffled every cycle */
        key = Mix64((RunSeed(self) << 32) ^ (u64)(u32)cycle ^ 0xBB67AE8584CAA73BULL);

    void *pick = 0;
    i64 acc = 0;
    if (mode == TOM_FULL_RANDOM) {
        /* randomize the slots (like "Randomize template order"), then find
           the template that owns slot p, walking the usable ones in table order */
        u32 p = PermIndex((u32)r, (u32)N, key);
        u32 n = listed ? K : (u32)c->count;
        for (u32 j = 0; j < n; j++) {
            WST *w = c->data[listed ? v[j] : j];
            if (!listed && !IsUsable(w, ulDiff)) continue;
            if ((i64)p < acc + w->ctSpawnWeight) { pick = w->pepTemplateCopy; break; }
            acc += w->ctSpawnWeight;
        }
    } else if (listed) {
        /* randomize the order of the groups, keep each group together
           (exactly the order of version 1.0) */
        Shuffle32(v, K, key);
        for (u32 j = 0; j < K; j++) {
            WST *w = c->data[v[j]];
            if ((i64)r < acc + w->ctSpawnWeight) { pick = w->pepTemplateCopy; break; }
            acc += w->ctSpawnWeight;
        }
    } else {
        /* more usable templates than v[] holds: walk the whole table in the
           order of a random permutation of its rows (needs no memory), skipping
           the unusable rows - the usable ones come out in a uniformly random
           order, every one of them exactly once */
        u32 n = (u32)c->count;
        for (u32 j = 0; j < n; j++) {
            WST *w = c->data[FeistelPerm(j, n, key)];
            if (!IsUsable(w, ulDiff)) continue;
            if ((i64)r < acc + w->ctSpawnWeight) { pick = w->pepTemplateCopy; break; }
            acc += w->ctSpawnWeight;
        }
    }
    if (pick) *out = g_PtrToHandle(pick);
    return out;
}

/* ------------------------------------------------------------------------- */
/* the new meta data                                                         */
/* ------------------------------------------------------------------------- */
static const char s_enumName[]   = "ETemplateOrderMethod";
static const char s_v0[] = "TOM_DEFAULT",            s_d0[] = "Default";
static const char s_v1[] = "TOM_GROUPS",             s_d1[] = "Groups";
static const char s_v2[] = "TOM_FULL_RANDOM",        s_d2[] = "Full random";
static const char s_v3[] = "TOM_GROUPS_FULL_RANDOM", s_d3[] = "Groups full random";
static const char s_memDisp[]    = "Template order method";
static const char s_memName[]    = "ep_etomTemplateOrderMethod";
static const char s_memId[]      = "6";

#define ENUM_QWORDS   23
#define MEMBER_QWORDS 12
#define NEW_QWORDS    (STREAM_QWORDS + ENUM_QWORDS + MEMBER_QWORDS)

static u64 g_stream[NEW_QWORDS];
static u64 g_enumType;              /* receives the CDataType * of the enum */

static u32 BuildStream(const u64 *src)
{
    u32 n = 0;
    /* enum ETemplateOrderMethod (kind 1) */
    g_stream[n++] = 1;              /* entry: data type   */
    g_stream[n++] = 1;              /* kind: enum         */
    g_stream[n++] = 0;              /* (local index, unused for enums) */
    g_stream[n++] = (u64)s_enumName;
    g_stream[n++] = 0;              /* format             */
    g_stream[n++] = 4;              /* size               */
    g_stream[n++] = 0;
    g_stream[n++] = 0;
    g_stream[n++] = 0;              /* not a flags enum   */
    g_stream[n++] = 4;              /* values             */
    g_stream[n++] = TOM_DEFAULT;            g_stream[n++] = (u64)s_v0; g_stream[n++] = (u64)s_d0;
    g_stream[n++] = TOM_GROUPS;             g_stream[n++] = (u64)s_v1; g_stream[n++] = (u64)s_d1;
    g_stream[n++] = TOM_FULL_RANDOM;        g_stream[n++] = (u64)s_v2; g_stream[n++] = (u64)s_d2;
    g_stream[n++] = TOM_GROUPS_FULL_RANDOM; g_stream[n++] = (u64)s_v3; g_stream[n++] = (u64)s_d3;
    g_stream[n++] = (u64)&g_enumType;

    /* original entries up to the ep_fRndBase member record */
    for (u32 i = 0; i < IDX_INSERT; i++) {
        u64 q = src[i];
        if (i == IDX_FORMAT)   q = NEW_FORMAT;
        if (i == IDX_NMEMBERS) q = 5;
        g_stream[n++] = q;
    }
    /* member: ETemplateOrderMethod ep_etomTemplateOrderMethod  (after "Randomize template order") */
    g_stream[n++] = 1;                       /* type by name   */
    g_stream[n++] = (u64)s_enumName;
    g_stream[n++] = NEW_MEMBER_ID;           /* member ID      */
    g_stream[n++] = (u64)s_memDisp;          /* display name   */
    g_stream[n++] = (u64)s_memName;          /* member name    */
    g_stream[n++] = (u64)s_memId;            /* ID string      */
    g_stream[n++] = src[IDX_BOOLREC + 6];    /* group: "" (the engine's own empty string) */
    g_stream[n++] = MEMBER_OFFSET;           /* offset         */
    g_stream[n++] = 0;                       /* flags          */
    g_stream[n++] = 0;
    g_stream[n++] = 0;
    g_stream[n++] = 0;                       /* no attributes  */
    /* rest of the original stream, including the terminating -1 */
    for (u32 i = IDX_INSERT; i < STREAM_QWORDS; i++) g_stream[n++] = src[i];
    return n;
}

#endif
