/* Prints the picks of the three new modes for a set of deterministic tables.
   Built once against version 1.0 (-Iv1) and once against the current source
   (-I../src); the outputs must match for tables of up to 512 usable rows. */
#include <stdio.h>
#include <stdlib.h>
#include "mso_logic.h"

static void *hv[1 << 16]; static u32 hn = 1;
static void *H2P(u32 h) { return (h && h < hn) ? hv[h] : 0; }
static u32 P2H(void *p) { if (!p) return 0; for (u32 i = 1; i < hn; i++) if (hv[i] == p) return i; hv[hn] = p; return hn++; }
static void Warn(const char *f, ...) { (void)f; }
static u32 *Orig(void *s, u32 *o, i32 a, i32 b, u32 c) { (void)s; (void)a; (void)b; (void)c; *o = 0; return o; }
typedef struct T { void **vt; i32 ok; i32 idx; } T;
static i32 OkFn(void *s) { return ((T *)s)->ok; }
static void *vt[64];
static Target tg;

int main(int argc, char **argv)
{
    int maxRows = argc > 1 ? atoi(argv[1]) : 600;
    vt[0x138 / 8] = (void *)OkFn;
    tg.mseVtable = 0xA0; tg.spawnerVtable = 0xB0; tg.WarnStr = (u64)"w";
    g_T = &tg; g_HandleToPtr = H2P; g_PtrToHandle = P2H; g_Warn = Warn; g_origGetTemplate = Orig;
    g_rng = 1; g_fallbackSeed = 2;
    int sizes[] = {1, 2, 3, 7, 16, 50, 130, 300, 511, 512, 540, 600};
    for (unsigned si = 0; si < sizeof sizes / sizeof *sizes; si++) {
        int n = sizes[si];
        if (n > maxRows) continue;
        T *t = calloc(n, sizeof *t); WST *w = calloc(n, sizeof *w); WST **pp = calloc(n, sizeof *pp);
        u64 s = 77 + si;
        for (int i = 0; i < n; i++) {
            u32 r = (u32)(SplitMix(&s) >> 40);
            t[i].vt = vt; t[i].idx = i; t[i].ok = r % 23 != 0;
            w[i].pepTemplateCopy = &t[i]; w[i].ctSpawnWeight = (int)(r % 4);
            w[i].ulDifficulty = (r % 13 == 0) ? 0x10 : 0x18;
            pp[i] = (r % 41 == 0) ? 0 : &w[i];
        }
        Container c = {pp, n};
        u8 props[0x98] = {0}, mse[0x60] = {0}, spw[0x150] = {0};
        *(u32 *)(props + 0x88) = P2H(&c);
        *(u8 **)(props + 0x08) = mse; *(u64 *)mse = 0xA0;
        *(u32 *)(mse + 0x5c) = P2H(spw); *(u64 *)spw = 0xB0;
        *(u32 *)(spw + SEED_OFFSET) = 0x1234567u + n;   /* fixed run seed */
        *(u32 *)(props + 0x90) = 0x3f000000u + n;       /* random base    */
        for (int mode = 1; mode <= 3; mode++)
            for (int cm = 0; cm < 2; cm++) {
                int ctMax = cm ? 37 : 100000;
                printf("n=%d mode=%d ctMax=%d:", n, mode, ctMax);
                *(i32 *)(props + 0x94) = mode;
                for (int sl = 0; sl < 3 * n + 5 && sl < 2000; sl++) {
                    u32 h = 0;
                    MSO_GetTemplate(props, &h, sl, ctMax, 0x8);
                    T *p = (T *)H2P(h);
                    printf(" %d", p ? p->idx : -1);
                }
                printf("\n");
            }
    }
    return 0;
}
