/* MultiPath marker for Sam2017 (Serious Sam Fusion 2017): the branching path marker of Serious Sam 2, rebuilt as a new
   engine class pair derived from the path marker:
     CMultiPathMarkerProperties : CPathMarkerProperties   (0xc8 bytes)  "Next markers" - a list instead of one next marker
     CMultiPathMarkerEntity     : CPathMarkerEntity       (0x98 bytes)  the resolved list of next marker entities
   The classes are described to the engine by a metadata stream (MPM_meta) that is registered from the engine's own
   static initializer of the path marker module (sinit hook -> MPM_RegisterMeta), so they exist before any world loads.

   Besides the new classes, a few functions of ALL path marker classes are replaced (vtable patches in the config), so
   that plain path markers and other path users understand a MultiPath marker as their neighbour:
     MPM_Props_Unlink             CPathMarkerProperties  vtbl[15]   a marker is deleted: relink around it
     MPM_PathMarkerEntity_Link    CPathMarkerEntity      vtbl[36]   (+ 2 tail calls) keep a MultiPath previous marker
     MPM_Entity_RenderPrimitive   CPathMarkerEntity      vtbl[71]   editor arrows to every next marker
     MPM_Entity_GetPathMarkers    CPathMarkerEntity      vtbl[164]  all markers of the path (branches included)
     MPM_PathWalker_Next/_Turn    path walker            vtbl[23/24] continue at a random one of the next markers

   Built by build_pkg.py: clang --target=x86_64-pc-windows-msvc -O2 -fno-builtin, one COFF object, linked at a nominal
   address above the executable; every eng_* symbol is an address of the executable (cfg/editor5.json,
   cfg/unrestricted5.json). The two vtables start as copies of the engine's (build_pkg fills their zero entries). */

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;
typedef int s32;
typedef unsigned char u8;
typedef float f32;
typedef s32 INDEX;
#define NULL ((void *)0)

/* CStaticStackArray<T>: elements, count, allocated, allocation step */
typedef struct { u32 *sa_Array; INDEX sa_Count, sa_Allocated, sa_Step; } HandleArray;
typedef struct { void **sa_Array; INDEX sa_Count, sa_Allocated, sa_Step; } PtrArray;

typedef struct Entity Entity;

/* CPathMarkerProperties (0x90) + the MultiPath part */
typedef struct Props {
    const void *const *vtbl;
    Entity *bp_penEntity;                   /* 0x08 the entity made from these properties */
    u8 _base10[0x30 - 0x10];
    char *bp_strName;                       /* 0x30 */
    u8 _base38[0x80 - 0x38];
    u32 ep_hepNextMarker;                   /* 0x80 Handle<CPathMarkerProperties> */
    u32 ep_colColor;                        /* 0x84 */
    f32 ep_fRadius;                         /* 0x88 */
    u32 _pad8c;
    HandleArray ep_ahepNextMarkers;         /* 0x90 Next markers */
    HandleArray ep_ahepNextMarkersR1;       /* 0xa8 the list as Serious Sam 2 worlds store it */
    u32 ep_colColorR1;                      /* 0xc0 the colour as Serious Sam 2 worlds store it */
    u32 _padc4;
} Props;

/* CPathMarkerEntity (0x80) + the MultiPath part */
struct Entity {
    const void *const *vtbl;
    u8 _base08[0x28 - 0x08];
    char *en_strName;                       /* 0x28 */
    u8 _base30[0x58 - 0x30];
    u32 en_ul58;                            /* 0x58 */
    u32 en_henNextMarker;                   /* 0x5c Handle<CPathMarkerEntity> */
    u32 en_henPrevMarker;                   /* 0x60 */
    f32 en_fRadius;                         /* 0x64 */
    HandleArray en_aul68;                   /* 0x68 */
    HandleArray en_ahenNextMarkers;         /* 0x80 */
};

/* the object that walks along path markers (vtable 0x14219cfa0 in the editor): this->0x48 -> state, state->0xf0 =
   the marker it is at */
typedef struct { u8 _a[0xf0]; Entity *penMarker; } WalkerState;
typedef struct { u8 _a[0x48]; WalkerState **ppState; } PathWalker;

typedef struct { void *ptType; void *ptr; } MetaPtr;    /* CMetaPointer */
typedef struct { f32 q[4]; f32 vPos[3]; f32 _pad[9]; } Placement;

/* --------------------------------------------------------------------------------------------- engine (cfg symbols) */
extern void eng_Free(void *p);
extern void eng_DeleteSized(void *p, u64 size);
extern void *eng_HandleToPointer(u32 h);
extern u32 eng_PointerToHandle(void *p);
extern int eng_meta_IsDerivedFrom(void *ptType, void *ptBase);
extern void eng_meta_register(const void *stream);
extern void *eng_meta_NewArray(u32 size, u32 count, void *ptType);
extern void *eng_meta_NewObject(u32 size, void *ptType);
extern INDEX eng_meta_ArrayCount(void *p);
extern void eng_meta_ReleaseObject(void *p, void *ptType);
extern void eng_meta_Destruct(void);            /* metadata function-table entries, only referenced */
extern void eng_meta_DynamicPointer(void);
extern MetaPtr *eng_MetaPointer_Set(MetaPtr *ret, void *ptType, void *p);
extern void eng_HandleArray_SetAllocation(HandleArray *a, INDEX n);
extern void eng_PtrArray_SetAllocation(PtrArray *a, INDEX n);
extern void eng_CString_Assign(char **dst, char **src);
extern void eng_PathMarkerProps_Construct(Props *pp);
extern void *eng_PathMarkerProps_Destroy(Props *pp, u32 flags);
extern void eng_PathMarkerProps_Unlink(Props *pp);
extern void eng_PathMarkerEntity_Construct(Entity *pen);
extern void *eng_PathMarkerEntity_Destroy(Entity *pen, u32 flags);
extern void eng_PathMarkerEntity_ApplyProps(Entity *pen, Props *pp);
extern void eng_PathMarkerEntity_Link(Entity *pen, Props *pp);
extern Props *eng_Entity_GetProperties(Entity *pen);
extern void eng_Props_Update(Props *pp);
extern void eng_Entity_ReportWarning(Entity *pen, const char *msg);
extern void eng_Prim_AddArrow(void *prim, const f32 *vFrom, const f32 *vTo, f32 fWidth, f32 fHead, u32 col);
extern void eng_PathWalker_Next(PathWalker *pw);
extern void eng_PathWalker_Turn(PathWalker *pw);
extern int eng_gbFlipPrimitive;
extern u32 eng_iRandomMul, eng_iRandomShift;
extern void *eng_ptPathMarkerEntityType;

/* ------------------------------------------------------------------------------------------------------ our data */
void *MPM_ptPropsType;                      /* the registered CMultiPathMarkerProperties type */
void *MPM_ptEntityType;                     /* the registered CMultiPathMarkerEntity type */

void *MPM_Props_GetType(Props *pp);
void *MPM_Props_Destroy(Props *pp, u32 flags);
void *MPM_Entity_GetType(Entity *pen);
void MPM_Props_Unlink(Props *pp);
void *MPM_Entity_Destroy(Entity *pen, u32 flags);
void MPM_Entity_ApplyProps(Entity *pen, Props *pp);
void MPM_Entity_Link(Entity *pen, Props *pp);
void MPM_Entity_RenderPrimitive(Entity *pen, u8 *prim);
void MPM_Entity_GetPathMarkers(Entity *pen, HandleArray *ahen);

/* our classes' vtables: build_pkg copies the engine's CPathMarkerProperties / CPathMarkerEntity entries into the zeros */
const void *const g_PropsVtbl[43] = {
    [0] = MPM_Props_GetType, [1] = MPM_Props_Destroy, [3] = MPM_Entity_GetType, [15] = MPM_Props_Unlink,
};
const void *const g_EntityVtbl[165] = {
    [0] = MPM_Entity_GetType, [1] = MPM_Entity_Destroy, [33] = MPM_Entity_ApplyProps, [36] = MPM_Entity_Link,
    [71] = MPM_Entity_RenderPrimitive, [164] = MPM_Entity_GetPathMarkers,
};

typedef void *(*GetType_t)(void *);
typedef void *(*Destroy_t)(void *, u32);
#define GETTYPE(p) ((GetType_t)(p)->vtbl[0])(p)
#define IS_MPM_PROPS(pp) ((pp)->vtbl == g_PropsVtbl)
#define IS_MPM_ENTITY(pen) ((pen)->vtbl == g_EntityVtbl)

/* --------------------------------------------------------------------------------------------- stack arrays */
static void HA_Init(HandleArray *a) { a->sa_Array = NULL; a->sa_Count = 0; a->sa_Allocated = 0; a->sa_Step = 16; }
static void PA_Init(PtrArray *a) { a->sa_Array = NULL; a->sa_Count = 0; a->sa_Allocated = 0; a->sa_Step = 16; }
static void HA_Clear(HandleArray *a) {
    a->sa_Count = 0;
    if (a->sa_Array) { eng_Free(a->sa_Array); a->sa_Array = NULL; a->sa_Allocated = 0; }
}
static void HA_Push(HandleArray *a, u32 h) {
    if (a->sa_Count >= a->sa_Allocated)
        eng_HandleArray_SetAllocation(a, (a->sa_Count / a->sa_Step + 1) * a->sa_Step);
    a->sa_Array[a->sa_Count] = 0;           /* Handle<> constructor */
    a->sa_Array[a->sa_Count++] = h;
}
static int HA_Find(const HandleArray *a, u32 h) {
    const u32 *ah = a->sa_Array;
    INDEX ct = a->sa_Count;
    for (INDEX i = 0; i < ct; i++)
        if (ah[i] == h) return 1;
    return 0;
}
static void PA_Push(PtrArray *a, void *p) {
    if (a->sa_Count >= a->sa_Allocated)
        eng_PtrArray_SetAllocation(a, (a->sa_Count / a->sa_Step + 1) * a->sa_Step);
    a->sa_Array[a->sa_Count] = p;
    a->sa_Count++;
}

static const char k_strSelfLink[] = "Path marker links to itself as next marker! Clearing the link.\n";

/* ------------------------------------------------------------------------------------------------- helpers */
/* the Serious Sam 2 form of the properties (loaded into the R1 members) -> the Fusion form */
void MPM_Props_ConvertR1(Props *pp) {
    for (INDEX i = 0; i < pp->ep_ahepNextMarkersR1.sa_Count; i++) {
        u32 h = pp->ep_ahepNextMarkersR1.sa_Array[i];
        if (eng_HandleToPointer(h)) HA_Push(&pp->ep_ahepNextMarkers, h);
    }
    HA_Clear(&pp->ep_ahepNextMarkersR1);
    if (pp->ep_colColorR1) {
        pp->ep_colColor = pp->ep_colColorR1;
        pp->ep_colColorR1 = 0;
    }
}

/* adds the entity of the marker properties pp to the list (once, path markers only, not pen itself) */
void MPM_AddMarker(Entity *pen, HandleArray *ahen, Props *pp) {
    Entity *penN = pp->bp_penEntity;
    if (!penN || penN == pen) return;
    if (!eng_meta_IsDerivedFrom(GETTYPE(penN), eng_ptPathMarkerEntityType)) return;
    u32 h = eng_PointerToHandle(penN);
    if (HA_Find(ahen, h)) return;
    HA_Push(ahen, h);
}

/* the previous marker is a MultiPath marker that lists pen as one of its next markers: keep it as previous */
static void KeepPrevMarker(Entity *pen, u32 henPrev) {
    Entity *penPrev = eng_HandleToPointer(henPrev);
    if (!penPrev || penPrev == pen || !IS_MPM_ENTITY(penPrev)) return;
    for (INDEX i = 0; i < penPrev->en_ahenNextMarkers.sa_Count; i++)
        if (eng_HandleToPointer(penPrev->en_ahenNextMarkers.sa_Array[i]) == pen) {
            pen->en_henPrevMarker = henPrev;
            return;
        }
}

/* ------------------------------------------------------------------------------------------------- properties */
void *MPM_Props_GetType(Props *pp) { (void)pp; return MPM_ptPropsType; }

void *MPM_Props_Destroy(Props *pp, u32 flags) {
    pp->vtbl = g_PropsVtbl;
    HA_Clear(&pp->ep_ahepNextMarkers);
    HA_Clear(&pp->ep_ahepNextMarkersR1);
    eng_PathMarkerProps_Destroy(pp, 0);
    if (flags & 1) eng_DeleteSized(pp, sizeof(Props));
    return pp;
}

void *MPM_Entity_GetType(Entity *pen) { (void)pen; return MPM_ptEntityType; }

/* a marker is deleted: its previous marker now points to its (first) next marker - also when the previous marker is
   a MultiPath marker (in every path marker properties class) */
void MPM_Props_Unlink(Props *pp) {
    int bMulti = IS_MPM_PROPS(pp);
    Entity *penPrev;
    if (!pp->bp_penEntity) {
        if (bMulti) return;
        eng_PathMarkerProps_Unlink(pp);
        return;
    }
    penPrev = eng_HandleToPointer(pp->bp_penEntity->en_henPrevMarker);
    if (bMulti) {
        if (!penPrev) return;
        MPM_Props_ConvertR1(pp);
    } else if (!penPrev || !IS_MPM_ENTITY(penPrev)) {
        eng_PathMarkerProps_Unlink(pp);
        return;
    }
    void *pNext = eng_HandleToPointer(pp->ep_hepNextMarker);
    if (!pNext && bMulti) {
        for (INDEX i = 0; i < pp->ep_ahepNextMarkers.sa_Count && !pNext; i++)
            pNext = eng_HandleToPointer(pp->ep_ahepNextMarkers.sa_Array[i]);
    }
    Props *ppPrev = eng_Entity_GetProperties(penPrev);
    if (!pNext || !ppPrev) return;
    u32 hNext = eng_PointerToHandle(pNext);
    if (!IS_MPM_ENTITY(penPrev)) {
        ppPrev->ep_hepNextMarker = hNext;
    } else {
        if (IS_MPM_PROPS(ppPrev)) MPM_Props_ConvertR1(ppPrev);
        int bChanged = 0;
        if (eng_HandleToPointer(ppPrev->ep_hepNextMarker) == pp) { ppPrev->ep_hepNextMarker = hNext; bChanged = 1; }
        for (INDEX i = 0; i < ppPrev->ep_ahepNextMarkers.sa_Count; i++)
            if (eng_HandleToPointer(ppPrev->ep_ahepNextMarkers.sa_Array[i]) == pp) {
                ppPrev->ep_ahepNextMarkers.sa_Array[i] = hNext;
                bChanged = 1;
            }
        if (!bChanged) return;
    }
    eng_Props_Update(ppPrev);
}

/* ----------------------------------------------------------------------------------------------------- entity */
void *MPM_Entity_Destroy(Entity *pen, u32 flags) {
    pen->vtbl = g_EntityVtbl;
    HA_Clear(&pen->en_ahenNextMarkers);
    eng_PathMarkerEntity_Destroy(pen, 0);
    if (flags & 1) eng_DeleteSized(pen, sizeof(Entity));
    return pen;
}

void MPM_Entity_ApplyProps(Entity *pen, Props *pp) {
    MPM_Props_ConvertR1(pp);
    eng_PathMarkerEntity_ApplyProps(pen, pp);
    eng_CString_Assign(&pen->en_strName, &pp->bp_strName);
}

/* resolves the next markers (the single one and the list) into en_ahenNextMarkers */
void MPM_Entity_Link(Entity *pen, Props *pp) {
    HandleArray *ahen = &pen->en_ahenNextMarkers;
    MPM_Props_ConvertR1(pp);
    ahen->sa_Count = 0;
    Props *ppNext = eng_HandleToPointer(pp->ep_hepNextMarker);
    if (ppNext == pp) {
        eng_Entity_ReportWarning(pen, k_strSelfLink);
        pp->ep_hepNextMarker = eng_PointerToHandle(NULL);
    } else if (ppNext) {
        MPM_AddMarker(pen, ahen, ppNext);
    }
    for (INDEX i = 0; i < pp->ep_ahepNextMarkers.sa_Count; i++) {
        Props *ppn = eng_HandleToPointer(pp->ep_ahepNextMarkers.sa_Array[i]);
        if (!ppn) continue;
        if (ppn == pp) {
            eng_Entity_ReportWarning(pen, k_strSelfLink);
            for (INDEX j = i; j < pp->ep_ahepNextMarkers.sa_Count - 1; j++)
                pp->ep_ahepNextMarkers.sa_Array[j] = pp->ep_ahepNextMarkers.sa_Array[j + 1];
            pp->ep_ahepNextMarkers.sa_Count--;
            i--;
            continue;
        }
        MPM_AddMarker(pen, ahen, ppn);
    }
    pen->en_henNextMarker = ahen->sa_Count ? ahen->sa_Array[0] : eng_PointerToHandle(NULL);
    /* the engine's link for the rest, with the single next marker cleared (it would link only that one) */
    u32 hepNext = pp->ep_hepNextMarker;
    u32 henPrev = pen->en_henPrevMarker;
    pp->ep_hepNextMarker = eng_PointerToHandle(NULL);
    eng_PathMarkerEntity_Link(pen, pp);
    pp->ep_hepNextMarker = hepNext;
    if (!eng_HandleToPointer(pen->en_henPrevMarker)) KeepPrevMarker(pen, henPrev);
    /* we are the previous marker of all our next markers */
    u32 hen = eng_PointerToHandle(pen);
    for (INDEX i = 0; i < ahen->sa_Count; i++) {
        Entity *penNext = eng_HandleToPointer(ahen->sa_Array[i]);
        if (penNext) penNext->en_henPrevMarker = hen;
    }
}

/* replaces CPathMarkerEntity's link in all path marker classes: a marker whose previous marker is a MultiPath marker
   keeps it (the engine would clear it, as that marker's single next marker is not this one) */
void MPM_PathMarkerEntity_Link(Entity *pen, Props *pp) {
    u32 henPrev = pen->en_henPrevMarker;
    eng_PathMarkerEntity_Link(pen, pp);
    if (eng_HandleToPointer(pen->en_henPrevMarker)) return;
    KeepPrevMarker(pen, henPrev);
}

/* all markers reachable from pen, depth first; apen is the work stack */
void MPM_CollectMarkers(Entity *pen, HandleArray *ahen, PtrArray *apen) {
    apen->sa_Count = 0;
    PA_Push(apen, pen);
    while (apen->sa_Count > 0) {
        Entity *p = apen->sa_Array[--apen->sa_Count];
        u32 h = eng_PointerToHandle(p);
        if (HA_Find(ahen, h)) continue;
        HA_Push(ahen, h);
        if (p && IS_MPM_ENTITY(p)) {
            for (INDEX i = p->en_ahenNextMarkers.sa_Count - 1; i >= 0; i--) {
                Entity *pn = eng_HandleToPointer(p->en_ahenNextMarkers.sa_Array[i]);
                if (pn) PA_Push(apen, pn);
            }
        } else {
            Entity *pn = eng_HandleToPointer(p->en_henNextMarker);
            if (pn) PA_Push(apen, pn);
        }
    }
}

/* all markers of the path: everything reachable from pen, then back over the previous markers */
void MPM_Entity_GetPathMarkers(Entity *pen, HandleArray *ahen) {
    PtrArray apen;
    PA_Init(&apen);
    HA_Clear(ahen);
    MPM_CollectMarkers(pen, ahen, &apen);
    for (Entity *penPrev = eng_HandleToPointer(pen->en_henPrevMarker); penPrev;
         penPrev = eng_HandleToPointer(penPrev->en_henPrevMarker)) {
        u32 h = eng_PointerToHandle(penPrev);
        if (HA_Find(ahen, h)) break;
        if (IS_MPM_ENTITY(penPrev)) MPM_CollectMarkers(penPrev, ahen, &apen);
        else HA_Push(ahen, h);
    }
    if (apen.sa_Array) eng_Free(apen.sa_Array);
}

/* an arrow from one marker to another (path markers only) */
void MPM_DrawArrow(u8 *prim, Entity *penFrom, Entity *penTo) {
    if (!penTo) return;
    if (!eng_meta_IsDerivedFrom(GETTYPE(penTo), eng_ptPathMarkerEntityType)) return;
    Placement pl;
    typedef Placement *(*GetPlacement_t)(Entity *, Placement *);
    f32 *p = ((GetPlacement_t)penFrom->vtbl[21])(penFrom, &pl)->vPos;
    f32 vFrom[3] = {p[0], p[1], p[2]};
    p = ((GetPlacement_t)penTo->vtbl[21])(penTo, &pl)->vPos;
    f32 vTo[3] = {p[0], p[1], p[2]};
    eng_Prim_AddArrow(prim, vFrom, vTo, 3.0f, 5.0f, 0xffffffff);
}

/* the editor primitive (CEditorPrimitiveRenderable) of a path marker: like the engine's, with arrows to every next
   marker of every marker of the path */
void MPM_Entity_RenderPrimitive(Entity *pen, u8 *prim) {
    *(u64 *)(prim + 0x18) = 1;
    *(u32 *)(prim + 0x14) = 1;
    *(u64 *)(prim + 0x20) = 0;
    double *m1 = (double *)(prim + 0x3708), *m2 = (double *)(prim + 0x37c8);   /* two 3x4 placements: identity */
    for (int i = 0; i < 12; i++) { double d = (i % 5 == 0) ? 1.0 : 0.0; m1[i] = d; m2[i] = d; }
    double *v = (double *)(prim + 0x3868);
    v[0] = 0.0; v[1] = 0.0; v[2] = 0.0;
    *(u32 *)(prim + 0x40f8) &= ~0xc39u;
    *(u32 *)(prim + 0x46a0) &= ~0xc39u;
    volatile f32 fOne = 1.0f;
    f32 *uv = (f32 *)(prim + 0x34);
    f32 fA, fB;
    if (eng_gbFlipPrimitive) { fB = fOne - uv[1]; fA = fOne - uv[0]; uv[0] = 1.0f; uv[1] = 1.0f; }
    else { fB = uv[0]; fA = uv[1]; *(u64 *)uv = 0; }
    HandleArray ahen;
    HA_Init(&ahen);
    ((void (*)(Entity *, HandleArray *))pen->vtbl[164])(pen, &ahen);
    for (INDEX i = 0; i < ahen.sa_Count; i++) {
        Entity *penM = eng_HandleToPointer(ahen.sa_Array[i]);
        if (!penM) continue;
        if (!eng_meta_IsDerivedFrom(GETTYPE(penM), eng_ptPathMarkerEntityType)) continue;
        if (IS_MPM_ENTITY(penM)) {
            for (INDEX k = 0; k < penM->en_ahenNextMarkers.sa_Count; k++)
                MPM_DrawArrow(prim, penM, eng_HandleToPointer(penM->en_ahenNextMarkers.sa_Array[k]));
        } else {
            MPM_DrawArrow(prim, penM, eng_HandleToPointer(penM->en_henNextMarker));
        }
    }
    if (eng_gbFlipPrimitive) { uv[0] = fOne - fA; uv[1] = fOne - fB; }
    else { uv[0] = fB; uv[1] = fA; }
    ahen.sa_Count = 0;
    if (ahen.sa_Array) eng_Free(ahen.sa_Array);
}

/* ----------------------------------------------------------------------------------------------- path walkers */
/* the engine's random number generator (it is inlined in the engine too): shift register xor multiplicative */
static u32 Random(void) {
    u32 s = eng_iRandomShift;
    s = ((((s << 3) ^ s) & 0xfffffff8u) << 28) | (s >> 1);
    u32 m = eng_iRandomMul * 0x48c27395;
    eng_iRandomShift = s;
    eng_iRandomMul = m;
    return s ^ m;
}

/* a walker at a MultiPath marker: the engine follows en_henNextMarker, so for the call set it to a random one of the
   (existing) next markers, then restore it */
static void Walk(PathWalker *pw, void (*pfn)(PathWalker *)) {
    Entity *pen;
    if (!pw->ppState || !*pw->ppState || !(pen = (*pw->ppState)->penMarker) || !IS_MPM_ENTITY(pen) ||
        pen->en_ahenNextMarkers.sa_Count <= 1) {
        pfn(pw);
        return;
    }
    u32 *ph = &pen->en_henNextMarker;
    u32 henSaved = pen->en_henNextMarker;
    u32 *ah = pen->en_ahenNextMarkers.sa_Array;
    u32 ct = 0;
    for (INDEX i = 0; i < pen->en_ahenNextMarkers.sa_Count; i++)
        if (eng_HandleToPointer(ah[i])) ct++;
    u32 *phNew = ph;
    if (ct) {
        u32 k = (u32)(((u64)Random() * ct) >> 32);
        for (INDEX i = 0; i < pen->en_ahenNextMarkers.sa_Count; i++)
            if (eng_HandleToPointer(ah[i]) && k-- == 0) { phNew = &ah[i]; break; }
    }
    *ph = *phNew;
    pfn(pw);
    *ph = henSaved;
}
void MPM_PathWalker_Next(PathWalker *pw) { Walk(pw, eng_PathWalker_Next); }
void MPM_PathWalker_Turn(PathWalker *pw) { Walk(pw, eng_PathWalker_Turn); }

/* ------------------------------------------------------------------------------------- metadata callbacks */
static void Props_Init(Props *pp) {
    if (!pp) return;
    eng_PathMarkerProps_Construct(pp);
    pp->vtbl = g_PropsVtbl;
    HA_Init(&pp->ep_ahepNextMarkers);
    HA_Init(&pp->ep_ahepNextMarkersR1);
    pp->ep_colColorR1 = 0;
    pp->ep_colColor = 0xffff00ff;
}
static void Entity_Init(Entity *pen) {
    if (!pen) return;
    eng_PathMarkerEntity_Construct(pen);
    pen->vtbl = g_EntityVtbl;
    pen->en_ul58 = 0; pen->en_henNextMarker = 0; pen->en_henPrevMarker = 0; pen->en_fRadius = 0.0f;
    HA_Init(&pen->en_aul68);
    HA_Init(&pen->en_ahenNextMarkers);
}
void MPM_Props_Construct(MetaPtr *mp) { Props_Init(mp->ptr); }
void MPM_Entity_Construct(MetaPtr *mp) { Entity_Init(mp->ptr); }

/* new object (ct < 0) or array of ct objects */
MetaPtr *MPM_Props_New(MetaPtr *ret, void *ptType, INDEX ct) {
    void *ptAlloc = MPM_ptPropsType;
    Props *pp;
    if (ct >= 0) {
        pp = eng_meta_NewArray(ct * sizeof(Props), ct, ptAlloc);
        for (INDEX i = 0; i < ct; i++) Props_Init(&pp[i]);
    } else {
        pp = eng_meta_NewObject(sizeof(Props), ptAlloc);
        Props_Init(pp);
    }
    eng_MetaPointer_Set(ret, ptType, pp);
    return ret;
}
MetaPtr *MPM_Entity_New(MetaPtr *ret, void *ptType, INDEX ct) {
    void *ptAlloc = MPM_ptEntityType;
    Entity *pen;
    if (ct >= 0) {
        pen = eng_meta_NewArray(ct * sizeof(Entity), ct, ptAlloc);
        for (INDEX i = 0; i < ct; i++) Entity_Init(&pen[i]);
    } else {
        pen = eng_meta_NewObject(sizeof(Entity), ptAlloc);
        Entity_Init(pen);
    }
    eng_MetaPointer_Set(ret, ptType, pen);
    return ret;
}
void MPM_Props_Delete(MetaPtr *mp, INDEX ct) {
    Props *pp = mp->ptr;
    if (!pp) return;
    if (ct >= 0) {
        INDEX n = eng_meta_ArrayCount(pp);
        for (INDEX i = 0; i < n; i++) ((Destroy_t)pp[i].vtbl[1])(&pp[i], 0);
    } else {
        eng_meta_ReleaseObject(pp, GETTYPE(pp));
        ((Destroy_t)pp->vtbl[1])(pp, 0);
    }
    eng_Free(pp);
}
void MPM_Entity_Delete(MetaPtr *mp, INDEX ct) {
    Entity *pen = mp->ptr;
    if (!pen) return;
    if (ct >= 0) {
        INDEX n = eng_meta_ArrayCount(pen);
        for (INDEX i = 0; i < n; i++) ((Destroy_t)pen[i].vtbl[1])(&pen[i], 0);
    } else {
        eng_meta_ReleaseObject(pen, GETTYPE(pen));
        ((Destroy_t)pen->vtbl[1])(pen, 0);
    }
    eng_Free(pen);
}

/* ------------------------------------------------------------------------------------------ metadata stream */
/* The engine's flat metadata format, one 64-bit word per item (as in the engine's own streams):
     type      1, kind, index, name, 0, size, 0, 0, <kind data>       kind 12 handle (1, target name),
                                                                    kind 7 stack array (2, element type index),
                                                                    kind 2 pointer (1, target name), kind 5 class:
     class     ..., 5 / 3, size, flags, 0, 1, base name, editor name, S-name, member count, members,
               0, attribute count, (name, value)..., function count, (flag, function)..., &type pointer
     member    2, type index | 1, type name;  id, editor name ("" = hidden), member name, serialized id (a string:
               the id, or an old member name), group ("" = none), offset, edit flags, 0, 0, attribute count
   The R1 members load the Serious Sam 2 form of a world (serialized under ep_ahepNextMarkers' old id / "ep_colColor");
   MPM_Props_ConvertR1 moves them to the Fusion members. Function flags: 0x200000 construct, 0x400000 destruct,
   1 new, 8 dynamic pointer, 2 delete. The stream ends with -1. */
#define P(x) ((u64)(x))
const u64 MPM_meta[] = {
    /* 0 */ 1, 12, 0, P("Handle<CMultiPathMarkerProperties>"), 0, 4, 0, 0, 1, P("CMultiPathMarkerProperties"),
    /* 1 */ 1, 7, 1, P("CStaticStackArray<Handle<CMultiPathMarkerProperties> >"), 0, 24, 0, 0, 2, 0,
    /* 2 */ 1, 12, 2, P("Handle<CPathMarkerProperties>"), 0, 4, 0, 0, 1, P("CPathMarkerProperties"),
    /* 3 */ 1, 7, 3, P("CStaticStackArray<Handle<CPathMarkerProperties> >"), 0, 24, 0, 0, 2, 2,
    /* 4 */ 1, 5, 4, P("CMultiPathMarkerProperties"), 3, 200, 0x100000, 0,
            1, P("CPathMarkerProperties"), P("MultiPath marker"), P("SMultiPathMarkerProperties"), 3,
            2, 3, 3, P("Next markers"), P("ep_ahepNextMarkers"), P("3"), P(""), 0x90, 50, 0, 0, 0,
            2, 1, 1, P(""), P("ep_ahepNextMarkersR1"), P("1"), P(""), 0xa8, 0, 0, 0, 0,
            1, P("COLOR"), 2, P(""), P("ep_colColorR1"), P("ep_colColor"), P(""), 0xc0, 0, 0, 0, 0,
            0, 2, P("thumbnail"), P("Content/SeriousEditor/Thumbnails/MultiPathMarker.tga"),
               P("category"), P("Characters"),
            5, 0x200000, P(MPM_Props_Construct), 0x400000, P(eng_meta_Destruct), 1, P(MPM_Props_New),
               8, P(eng_meta_DynamicPointer), 2, P(MPM_Props_Delete), P(&MPM_ptPropsType),
    /* 5 */ 1, 12, 5, P("Handle<CPathMarkerEntity>"), 0, 4, 0, 0, 1, P("CPathMarkerEntity"),
    /* 6 */ 1, 7, 6, P("CStaticStackArray<Handle<CPathMarkerEntity> >"), 0, 24, 0, 0, 2, 5,
    /* 7 */ 1, 5, 7, P("CMultiPathMarkerEntity"), 5, 152, 0, 0,
            1, P("CPathMarkerEntity"), P("MultiPath marker"), P("SMultiPathMarkerEntity"), 1,
            2, 6, 3, P(""), P("en_ahenNextMarkers"), P("3"), P(""), 0x80, 0, 0, 0, 0,
            0, 1, P("SaveData_NotNeeded"), P(""),
            5, 0x200000, P(MPM_Entity_Construct), 0x400000, P(eng_meta_Destruct), 1, P(MPM_Entity_New),
               8, P(eng_meta_DynamicPointer), 2, P(MPM_Entity_Delete), P(&MPM_ptEntityType),
    /* 8 */ 1, 12, 8, P("Handle<CMultiPathMarkerEntity>"), 0, 4, 0, 0, 1, P("CMultiPathMarkerEntity"),
    /* 9 */ 1, 7, 9, P("CStaticStackArray<Handle<CMultiPathMarkerEntity> >"), 0, 24, 0, 0, 2, 8,
    /* 10 */ 1, 2, 10, P("CEditorPrimitiveRenderable*"), 0, 8, 0, 0, 1, P("CEditorPrimitiveRenderable"),
    (u64)-1,
};

/* replaces the engine's call to the metadata registration in the path marker module's static initializer: the engine's
   stream first (our classes derive from its classes), then ours */
void MPM_RegisterMeta(const void *stream) {
    eng_meta_register(stream);
    eng_meta_register(MPM_meta);
}
