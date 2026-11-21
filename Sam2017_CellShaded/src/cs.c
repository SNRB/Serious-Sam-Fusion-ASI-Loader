/* CellShaded for Sam2017 (Serious Sam Fusion 2017): a cel (toon) shaded variant of the Standard shader.

     CCellShadedShaderArgs : CStandardShaderArgs   (0x6f0)  the Standard material arguments + 5 "Cel shading" ones
     CCellShadedShader     : CStandardShader       (0x60)   renders with the CellShaded pixel programs

   The shader's render function is a copy of the Standard shader's (cfg 'clones': CS_StandardRender), in which
     - the pixel program cache (an array off __ImageBase) is ours, CS_aPrograms, and so is the program getter
       (CS_GetProgram: our source, our entry point "CellShadedPS"),
     - the program's debug name is ours ("CellShaded"),
     - the unlit constant count is 25 instead of 22 (our c22..c24),
     - the "has lights" test is routed through CS_CopyHook_HasLights, which fills c22..c24 from our arguments.
   The pixel shader source (CellShadedShader.psh) is embedded, and also registered as
   "Sources/Shaders/CellShadedShader.psh" (a file "Shaders/CellShadedShader.psh" in the game's data overrides it).
   Registration: the engine's shader registration ends with a tail call; that jump is patched to CS_Register, which
   registers the service, the source and the metadata once, then creates our shader under the name "CellShaded".

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

/* CShaderArgVal<T>: vtable, flags, value */
typedef struct { const void *vtbl; u32 ulFlags; u32 _pad; union { f32 f; u32 ul; void *p; } v; } ArgVal;
/* a resource value of an argument (texture): flag 1 = not resolved yet, vtbl[4] resolves it */
typedef struct { const void *const *vtbl; u8 ubFlags; } Resource;

/* CStandardShaderArgs (0x678) + the cel shading arguments */
typedef struct Args {
    const void *const *vtbl;
    u8 _a008[0x140 - 0x008];
    ArgVal sp_fBaseSpec;                    /* 0x140 */
    ArgVal sp_fFresnel;                     /* 0x158 */
    u8 _a170[0x230 - 0x170];
    ArgVal sp_colCoat;                      /* 0x230 */
    u8 _a248[0x308 - 0x248];
    ArgVal sp_ptxHeight;                    /* 0x308 */
    ArgVal sp_fHeightBias;                  /* 0x320 */
    u8 _a338[0x380 - 0x338];
    ArgVal sp_ptxNormal;                    /* 0x380 */
    ArgVal sp_idNormalUVMap;                /* 0x398 */
    ArgVal sp_fBumpStrength;                /* 0x3b0 */
    u8 _a3c8[0x440 - 0x3c8];
    ArgVal sp_ptxDetail;                    /* 0x440 */
    ArgVal sp_idDetailUVMap;                /* 0x458 */
    ArgVal sp_fDetStrength;                 /* 0x470 */
    u8 _a488[0x678 - 0x488];
    ArgVal sp_colCelShade;                  /* 0x678 shade color */
    ArgVal sp_fCelShadeIntensity;           /* 0x690 shade intensity */
    ArgVal sp_fCelSharpness;                /* 0x6a8 shadow sharpness */
    ArgVal sp_fCelThreshold;                /* 0x6c0 shadow threshold */
    ArgVal sp_fCelFlatness;                 /* 0x6d8 ambient flatness */
} Args;

/* CStandardShader (0x60) */
typedef struct Shader {
    const void *const *vtbl;
    u8 _s008[0x58 - 0x08];
    void *sh_pDefaultArgs;                  /* 0x58 the arguments of a material without its own */
} Shader;

/* CService */
typedef struct {
    const char *svc_strName, *svc_strGroup;
    u32 svc_ul10, svc_ulPhase;
    u64 svc_ulPriority;
    void *svc_p20, *svc_p28;
    void (*svc_pInit)(void);
    void (*svc_pEnd)(void);
} Service;

typedef struct { void *ptType; void *ptr; } MetaPtr;    /* CMetaPointer */
typedef struct { void *es_pexError; void *es_pesParent; } ErrorScope;   /* the engine's error (exception) context */

/* where the copy of the render function keeps things in its frame (rbp): checked by its magic before use */
typedef struct { INDEX iConstants, iLight, iFlags; u32 ulMagic; } CopyLayout;
#define LAYOUT_MAGIC 0x43534652                 /* 'RFSC' */

/* --------------------------------------------------------------------------------------------- engine (cfg symbols) */
extern void eng_Free(void *p);
extern void eng_DeleteSized(void *p, u64 size);
extern int eng_meta_IsDerivedFrom(void *ptType, void *ptBase);
extern void eng_meta_register(const void *stream);
extern void *eng_meta_NewArray(u32 size, u32 count, void *ptType);
extern void *eng_meta_NewObject(u32 size, void *ptType);
extern INDEX eng_meta_ArrayCount(void *p);
extern void eng_meta_ReleaseObject(void *p, void *ptType);
extern void eng_meta_Destruct(void);            /* metadata function-table entries, only referenced */
extern void eng_meta_DynamicPointer(void);
extern void eng_meta_DeleteObject(void);
extern void eng_StandardShaderArgs_Fn800(void);
extern void eng_StandardShaderArgs_Fn1000000(void);
extern MetaPtr *eng_MetaPointer_Set(MetaPtr *ret, void *ptType, void *p);
extern void *eng_meta_FindType(void *ret, const char *strName);
extern MetaPtr *eng_meta_New(MetaPtr *ret, void *pType);
extern void eng_Name_Set(const char **pstr, const char *str);
extern void eng_CString_SetChars(char **pstr, const char *str);
extern void eng_CString_Free(char **pstr);
extern void eng_Warning(const char *strFormat, u32 ulArgType, const void *pArg);
extern void eng_ErrorScope_Begin(ErrorScope *pes, void *pContext);
extern void eng_ErrorScope_End(ErrorScope *pes);
extern char *eng_LoadTextFile(ErrorScope *pes, const char *strFileName);
extern void eng_RegisterEmbeddedFile(const char *strFileName, const char *strText);
extern void eng_RegisterShadersTail(void *pShaders);
extern void eng_RegisterShader(void *pShaders, u32 ulID, const char *strName, void *psh);
extern void eng_atexit(void (*pfn)(void));
extern char **eng_Shader_GetDefines(char **ret, int a, int b);
extern u32 eng_Shader_CompileProgram(void *pManager, int iType, const char *strSource, const char *strEntry,
                                     const char *strDefines, int iFlags);
extern void eng_Shader_ReleasePrograms(INDEX ct, u32 *ahPrograms, int iFlags);
extern int eng_HasLights(void *prc);
extern void eng_StandardShaderArgs_Construct(Args *pa);
extern void eng_StandardShaderArgs_Destruct(Args *pa);
extern void eng_StandardShaderArgs_CopyFrom(Args *pa, Args *paOther);
extern void eng_StandardShaderArgs_Validate(Args *pa);
extern void eng_StandardShaderArgs_Fn80(void *p, void *a, u32 ulID);
extern void eng_StandardShader_Construct(Shader *psh);
extern void eng_ShaderArgVal_Construct(ArgVal *pv);
extern void eng_ShaderArgVal_Destruct(ArgVal *pv);
extern void eng_ShaderArgsCache_Construct(void *pc, Args *pa);
extern void eng_ShaderArgsCache_Destruct(void *pc);
extern const void *eng_vtblShaderArgValCOLOR[], *eng_vtblShaderArgValFLOAT[];
extern void *eng_idNoUVMap;
extern int eng_bPrecompileShaders, eng_iParallaxMapping;
extern void *eng_pShaderManager;
extern void *eng_ptShaderType;
extern const char *eng_astrLightPermutation[16];
extern u8 eng_ecShaders[];

/* ------------------------------------------------------------------------------------------------------ our data */
void *CS_ptArgsType;                        /* the registered CCellShadedShaderArgs type */
char *CS_strSource;                         /* the pixel shader source in use (CString) */
u32 CS_aPrograms[1024];                     /* our pixel programs, by permutation (the copy's program cache) */
void *CS_ptShaderType;                      /* the registered CCellShadedShader type */
void *CS_pmcPostRead, *CS_pmcPostChange, *CS_pmcVerify;  /* the declared methods */
u8 CS_acDefaultArgs[0x810];                 /* CShaderArgsCache of the default arguments */
static int CS_bDefaultArgs;                 /* CS_argsDefault + CS_acDefaultArgs constructed (by the first shader) */
/* the default arguments (of a material without CellShaded arguments); 16-aligned like the engine's own objects */
static __declspec(align(16)) Args CS_argsDefault;
static int CS_bRegistered;                  /* RegisterOnce done */

void *CS_Args_GetType(Args *pa);
void *CS_Args_Destroy(Args *pa, u32 flags);
void CS_Args_Copy(Args *pa, Args *paOther);
void CS_Args_Validate(Args *pa);
void CS_Args_Precompile(Args *pa);
Args *CS_Args_Duplicate(Args *pa);
void *CS_Shader_GetType(Shader *psh);
extern void CS_StandardRender(void);        /* the copy (cfg 'clones') */

/* our classes' vtables: build_pkg copies the engine's CStandardShaderArgs / CStandardShader entries into the zeros */
const void *const g_csArgsVtbl[13] = {
    [0] = CS_Args_GetType, [1] = CS_Args_Destroy, [3] = CS_Args_Copy, [5] = CS_Args_Validate,
    [7] = CS_Args_Precompile, [10] = CS_Args_Duplicate,
};
const void *const g_csShaderVtbl[30] = {
    [0] = CS_Shader_GetType, [20] = CS_StandardRender,
};

void CS_Service_Init(void);
void CS_Service_End(void);
Service CS_svc = {0, 0, 0, 2, 100, 0, 0, CS_Service_Init, CS_Service_End};
volatile CopyLayout CS_layout = {0x2f0, 0x50, -0xc0, LAYOUT_MAGIC};

const char g_csName[] = "CellShaded";
static const char *const k_astrOptions[6] = {
    "HAS_SPECULARITY=1;", "HAS_COATING=1;", "HAS_NORMAL_MAP=1;", "HAS_PARALLAX=1;", "APPLY_FOZE=1;", "BAKING=1;",
};
static const char k_strEntry[] = "CellShadedPS";
static const char k_strFile[] = "Shaders/CellShadedShader.psh";
static const char k_strSource[] =
#include "CellShadedShader.psh.inc"
;

typedef void *(*GetType_t)(void *);
typedef void *(*Destroy_t)(void *, u32);
#define GETTYPE(p) ((GetType_t)(p)->vtbl[0])(p)

/* the copy calls this instead of the engine's "has lights" test (with the copy's own arguments); at that point the
   copy's rbp is its frame base and r14 the shader arguments - registers C cannot name, hence assembly */
__asm__(
    ".text\n"
    ".globl CS_CopyHook_HasLights\n"
    ".def CS_CopyHook_HasLights; .scl 2; .type 32; .endef\n"
    "CS_CopyHook_HasLights:\n"
    ".seh_proc CS_CopyHook_HasLights\n"
    "    subq $0x38, %rsp\n"
    ".seh_stackalloc 0x38\n"
    ".seh_endprologue\n"
    "    callq eng_HasLights\n"
    "    movl %eax, 0x30(%rsp)\n"
    "    movq %rbp, %rcx\n"
    "    movq %r14, %rdx\n"
    "    callq CS_SetConstants\n"
    "    movl 0x30(%rsp), %eax\n"
    "    addq $0x38, %rsp\n"
    "    retq\n"
    ".seh_endproc\n");

/* ------------------------------------------------------------------------------------------------- helpers */
static int IsCellArgs(Args *pa) {
    if (pa->vtbl == g_csArgsVtbl) return 1;
    void *pt = CS_ptArgsType;
    return pt && eng_meta_IsDerivedFrom(GETTYPE(pa), pt);
}
static f32 Clamp(f32 f, f32 fMin, f32 fMax) { return f >= fMin ? (f > fMax ? fMax : f) : fMin; }
static void *Resolve(ArgVal *pv) {
    Resource *pr = pv->v.p;
    if (!pr) return NULL;
    if (pr->ubFlags & 1) ((void (*)(Resource *))pr->vtbl[4])(pr);
    return pv->v.p;
}
/* sRGB -> linear */
static f32 ToLinear(u32 ub) {
    f32 f = ub * (1.0f / 255.0f);
    f32 f2 = f * f;
    return 0.603697f * f2 + 0.627612f * f2 * f - 0.231308f * f2 * f2;
}

static void Args_Init(Args *pa) {
    eng_StandardShaderArgs_Construct(pa);
    pa->vtbl = g_csArgsVtbl;
    eng_ShaderArgVal_Construct(&pa->sp_colCelShade);        pa->sp_colCelShade.vtbl = eng_vtblShaderArgValCOLOR;
    eng_ShaderArgVal_Construct(&pa->sp_fCelShadeIntensity); pa->sp_fCelShadeIntensity.vtbl = eng_vtblShaderArgValFLOAT;
    eng_ShaderArgVal_Construct(&pa->sp_fCelSharpness);      pa->sp_fCelSharpness.vtbl = eng_vtblShaderArgValFLOAT;
    eng_ShaderArgVal_Construct(&pa->sp_fCelThreshold);      pa->sp_fCelThreshold.vtbl = eng_vtblShaderArgValFLOAT;
    eng_ShaderArgVal_Construct(&pa->sp_fCelFlatness);       pa->sp_fCelFlatness.vtbl = eng_vtblShaderArgValFLOAT;
    pa->sp_colCelShade.v.ul = 0xffffffff;
    pa->sp_fCelShadeIntensity.v.f = 1.0f;
    pa->sp_fCelSharpness.v.f = 0.9f;
    pa->sp_fCelThreshold.v.f = 0.0f;
    pa->sp_fCelFlatness.v.f = 1.0f;
}
static void Args_Destruct(Args *pa) {
    pa->vtbl = g_csArgsVtbl;
    eng_ShaderArgVal_Destruct(&pa->sp_fCelFlatness);
    eng_ShaderArgVal_Destruct(&pa->sp_fCelThreshold);
    eng_ShaderArgVal_Destruct(&pa->sp_fCelSharpness);
    eng_ShaderArgVal_Destruct(&pa->sp_fCelShadeIntensity);
    eng_ShaderArgVal_Destruct(&pa->sp_colCelShade);
    eng_StandardShaderArgs_Destruct(pa);
}

u32 CS_GetProgram(u32 iPerm, char **pstrDefines, int iFlags);

/* ------------------------------------------------------------------------------------------------- arguments */
void *CS_Args_GetType(Args *pa) { (void)pa; return CS_ptArgsType; }

void *CS_Args_Destroy(Args *pa, u32 flags) {
    Args_Destruct(pa);
    if (flags & 1) eng_DeleteSized(pa, sizeof(Args));
    return pa;
}

void CS_Args_Copy(Args *pa, Args *paOther) {
    eng_StandardShaderArgs_CopyFrom(pa, paOther);
    if (!paOther || !IsCellArgs(paOther)) return;
    pa->sp_colCelShade.ulFlags = paOther->sp_colCelShade.ulFlags;
    pa->sp_colCelShade.v.ul = paOther->sp_colCelShade.v.ul;
    pa->sp_fCelShadeIntensity.ulFlags = paOther->sp_fCelShadeIntensity.ulFlags;
    pa->sp_fCelShadeIntensity.v.ul = paOther->sp_fCelShadeIntensity.v.ul;
    pa->sp_fCelSharpness.ulFlags = paOther->sp_fCelSharpness.ulFlags;
    pa->sp_fCelSharpness.v.ul = paOther->sp_fCelSharpness.v.ul;
    pa->sp_fCelThreshold.ulFlags = paOther->sp_fCelThreshold.ulFlags;
    pa->sp_fCelThreshold.v.ul = paOther->sp_fCelThreshold.v.ul;
    pa->sp_fCelFlatness.ulFlags = paOther->sp_fCelFlatness.ulFlags;
    pa->sp_fCelFlatness.v.ul = paOther->sp_fCelFlatness.v.ul;
}

void CS_Args_Validate(Args *pa) {
    eng_StandardShaderArgs_Validate(pa);
    pa->sp_fCelShadeIntensity.v.f = Clamp(pa->sp_fCelShadeIntensity.v.f, 0.0f, 16.0f);
    pa->sp_fCelSharpness.v.f = Clamp(pa->sp_fCelSharpness.v.f, 0.0f, 1.0f);
    pa->sp_fCelThreshold.v.f = Clamp(pa->sp_fCelThreshold.v.f, -1.0f, 1.0f);
    pa->sp_fCelFlatness.v.f = Clamp(pa->sp_fCelFlatness.v.f, 0.0f, 1.0f);
}

/* compiles the programs this material can need (as the Standard shader does for its own) */
void CS_Args_Precompile(Args *pa) {
    if (!CS_strSource || !*CS_strSource) return;
    void *idNone = eng_idNoUVMap;
    if (((void *(*)(Args *))pa->vtbl[6])(pa)) return;
    u32 aulPerm[32];
    for (INDEX i = 0; i < 32; i++) aulPerm[i] = 0;
    INDEX ct = 1;
    if (pa->sp_fBaseSpec.v.f > 0.001f || pa->sp_fFresnel.v.f > 0.0f) aulPerm[0] |= 0x10;
    if (pa->sp_colCoat.v.ul & 0xffffff) aulPerm[0] |= 0x20;
    if (Resolve(&pa->sp_ptxHeight) && pa->sp_idNormalUVMap.v.p != idNone && pa->sp_fHeightBias.v.f > 0.0f &&
        eng_iParallaxMapping > 0) {
        aulPerm[1] = aulPerm[0] | 0x80;
        ct = 2;
    }
    if ((Resolve(&pa->sp_ptxNormal) && pa->sp_idNormalUVMap.v.p != idNone && pa->sp_fBumpStrength.v.f > 0.0f) ||
        (Resolve(&pa->sp_ptxDetail) && pa->sp_idDetailUVMap.v.p != idNone && pa->sp_fDetStrength.v.f > 0.0f)) {
        for (INDEX i = 0; i < ct; i++) aulPerm[i] |= 0x40;
    }
    for (INDEX i = 0; i < ct; i++) aulPerm[ct + i] = aulPerm[i] | 0x100;
    char *strDefines;
    eng_Shader_GetDefines(&strDefines, 1, 1);
    for (INDEX i = 0; i < ct * 2; i++) {
        u32 ulPerm = aulPerm[i];
        for (u32 iLight = 1; iLight < 16; iLight++) {
            u32 ctDir = iLight & 3, ctPoint = iLight >> 2;
            if (!ctDir && ctPoint != 1) continue;
            u32 iPerm = iLight | ulPerm;
            if (CS_aPrograms[iPerm]) continue;
            CS_aPrograms[iPerm] = CS_GetProgram(iPerm, &strDefines, 0);
        }
    }
    eng_CString_Free(&strDefines);
}

Args *CS_Args_Duplicate(Args *pa) {
    Args *paNew = eng_meta_NewObject(sizeof(Args), CS_ptArgsType);
    if (paNew) {
        Args_Init(paNew);
        ((void (*)(Args *, Args *))paNew->vtbl[3])(paNew, pa);
    }
    return paNew;
}

void *CS_Shader_GetType(Shader *psh) { (void)psh; return CS_ptShaderType; }

/* -------------------------------------------------------------------------------------------- pixel programs */
static __forceinline char *Append(char *p, const char *pEnd, const char *strAdd) {
    if (strAdd) while (*strAdd && p < pEnd) *p++ = *strAdd++;
    *p = 0;
    return p;
}

/* the pixel program of a permutation: bits 0..3 lighting (the engine's), 4.. options (k_astrOptions) */
u32 CS_GetProgram(u32 iPerm, char **pstrDefines, int iFlags) {
    char str[1024];
    str[0] = 0;
    if (!CS_strSource || !*CS_strSource) return 0;
    char *p = str, *pEnd = str + sizeof(str) - 1;
    if (pstrDefines) p = Append(p, pEnd, *pstrDefines);
    for (INDEX i = 0; i < 6; i++)
        if ((iPerm >> i) & 0x10) p = Append(p, pEnd, k_astrOptions[i]);
    Append(p, pEnd, eng_astrLightPermutation[iPerm & 15]);
    return eng_Shader_CompileProgram(eng_pShaderManager, 1, CS_strSource, k_strEntry, str, iFlags);
}

/* ---------------------------------------------------------------------------------------------- the service */
/* the source: Shaders/CellShadedShader.psh if the game has it, else the embedded one; compiles all the programs if the
   engine precompiles its shaders */
void CS_Service_Init(void) {
    ErrorScope es;
    eng_ErrorScope_Begin(&es, eng_ecShaders);
    char *str = eng_LoadTextFile(&es, k_strFile);
    if (es.es_pexError) {
        const char *strError = ((const char *(*)(void *))(*(const void *const **)es.es_pexError)[2])(es.es_pexError);
        eng_Warning("%1\n", 0xabcd0009, strError ? strError : "");
    }
    eng_ErrorScope_End(&es);
    if (!str || !*str) str = (char *)k_strSource;
    eng_CString_SetChars(&CS_strSource, str);
    if (!eng_bPrecompileShaders) return;
    char *strDefines;
    eng_Shader_GetDefines(&strDefines, 1, 1);
    for (u64 iPerm = 1; iPerm < 1024; iPerm++) {
        if (iPerm >= 0x200 && (iPerm & 0x180)) continue;
        if ((iPerm & 0x80) && eng_iParallaxMapping <= 0) continue;
        if (!(iPerm & 3) && (iPerm & 12) != 4) continue;
        if (CS_aPrograms[iPerm]) continue;
        CS_aPrograms[iPerm] = CS_GetProgram((u32)iPerm, &strDefines, 0);
    }
    eng_CString_Free(&strDefines);
}

void CS_Service_End(void) { eng_Shader_ReleasePrograms(1024, CS_aPrograms, 0); }

/* c22 (shade color, intensity), c23 (terminator, edge, flatness, edge), c24 (flat ambient: the light's color) into the
   copy's constants */
void CS_SetConstants(u8 *pFrame, Args *pa) {
    u32 ulMagic = CS_layout.ulMagic;
    if (ulMagic != LAYOUT_MAGIC || !pFrame || !pa) return;
    f32 *c = (f32 *)(pFrame + CS_layout.iConstants);
    u8 *pLight = pFrame + CS_layout.iLight;
    u32 ulFlags = *(u32 *)(pFrame + CS_layout.iFlags);
    f32 fSharpness, fThreshold, fFlatness;
    if (IsCellArgs(pa)) {
        u32 col = pa->sp_colCelShade.v.ul;
        c[88] = ToLinear((col >> 16) & 0xff);
        c[89] = ToLinear((col >> 8) & 0xff);
        c[90] = ToLinear(col & 0xff);
        c[91] = Clamp(pa->sp_fCelShadeIntensity.v.f, 0.0f, 16.0f);
        fSharpness = Clamp(pa->sp_fCelSharpness.v.f, 0.0f, 1.0f);
        fThreshold = Clamp(pa->sp_fCelThreshold.v.f, -1.0f, 1.0f);
        fFlatness = Clamp(pa->sp_fCelFlatness.v.f, 0.0f, 1.0f);
    } else {
        c[88] = 1.0f; c[89] = 1.0f; c[90] = 1.0f; c[91] = 1.0f;
        fSharpness = 0.9f; fThreshold = 0.0f; fFlatness = 1.0f;
    }
    c[94] = fFlatness;
    f32 fEdge = 1.0f - fSharpness;
    fEdge = 0.5f * fEdge * fEdge;
    c[92] = fThreshold + fEdge;
    c[93] = fEdge;
    c[95] = fEdge;
    if (ulFlags & 1) {
        f32 *pfLight = (f32 *)(pLight + 0xe0);
        c[96] = pfLight[0] > 0.0f ? pfLight[0] : 0.0f;
        c[97] = pfLight[1] > 0.0f ? pfLight[1] : 0.0f;
        c[98] = pfLight[2] > 0.0f ? pfLight[2] : 0.0f;
        c[99] = 1.0f;
    } else {
        c[96] = c[97] = c[98] = c[99] = 0.0f;
    }
}

/* ------------------------------------------------------------------------------------- metadata callbacks */
static void CS_DefaultArgs_Destroy(void);

void CS_Args_Construct(MetaPtr *mp) {
    Args *pa = mp->ptr;
    if (pa) Args_Init(pa);
}
static void CS_Shader_Init(Shader *psh);
void CS_Shader_Construct(MetaPtr *mp) {
    Shader *psh = mp->ptr;
    if (psh) CS_Shader_Init(psh);
}
static void CS_Shader_Init(Shader *psh) {
    eng_StandardShader_Construct(psh);
    psh->vtbl = g_csShaderVtbl;
    if (!CS_bDefaultArgs) {
        Args_Init(&CS_argsDefault);
        eng_ShaderArgsCache_Construct(CS_acDefaultArgs, &CS_argsDefault);
        CS_bDefaultArgs = 1;
        eng_atexit(CS_DefaultArgs_Destroy);
    }
    psh->sh_pDefaultArgs = CS_acDefaultArgs;
}

/* new object (ct < 0) or array of ct objects */
MetaPtr *CS_Args_New(MetaPtr *ret, void *ptType, INDEX ct) {
    void *ptAlloc = CS_ptArgsType;
    Args *pa;
    if (ct >= 0) {
        pa = eng_meta_NewArray(ct * sizeof(Args), ct, ptAlloc);
        for (INDEX i = 0; pa && i < ct; i++) Args_Init(&pa[i]);
    } else {
        pa = eng_meta_NewObject(sizeof(Args), ptAlloc);
        if (pa) Args_Init(pa);
    }
    eng_MetaPointer_Set(ret, ptType, pa);
    return ret;
}
MetaPtr *CS_Shader_New(MetaPtr *ret, void *ptType, INDEX ct) {
    void *ptAlloc = CS_ptShaderType;
    Shader *psh;
    if (ct >= 0) {
        psh = eng_meta_NewArray(ct * sizeof(Shader), ct, ptAlloc);
        for (INDEX i = 0; psh && i < ct; i++) CS_Shader_Init(&psh[i]);
    } else {
        psh = eng_meta_NewObject(sizeof(Shader), ptAlloc);
        if (psh) CS_Shader_Init(psh);
    }
    eng_MetaPointer_Set(ret, ptType, psh);
    return ret;
}
void CS_Args_Delete(MetaPtr *mp, INDEX ct) {
    Args *pa = mp->ptr;
    if (!pa) return;
    if (ct >= 0) {
        INDEX n = eng_meta_ArrayCount(pa);
        for (INDEX i = 0; i < n; i++) ((Destroy_t)pa[i].vtbl[1])(&pa[i], 0);
    } else {
        eng_meta_ReleaseObject(pa, GETTYPE(pa));
        ((Destroy_t)pa->vtbl[1])(pa, 0);
    }
    eng_Free(pa);
}
/* function-table entry 0x80, as the Standard arguments' (903 = sp_bRoughnessMask) */
void CS_Args_Fn80(void *a, MetaPtr *mp) {
    if (!mp || !mp->ptr) return;
    eng_StandardShaderArgs_Fn80(mp->ptr, a, 903);
}

/* ------------------------------------------------------------------------------------------ metadata stream */
/* (the format is described in MultiPathMarker's mpm.c) kind 9 = a declared method: name, 0, 0, 0, 0, class count, class, return type,
   parameter count, parameters (1, type name | 2, type index; name, 0), &storage */
#define P(x) ((u64)(x))
const u64 CS_meta[] = {
    /* 0 */ 1, 2, 0, P("CMetaContext*"), 0, 8, 0, 0, 1, P("CMetaContext"),
    /* 1 */ 1, 9, 1, P("mdPostRead"), 0, 0, 0, 0, 1, P("CCellShadedShaderArgs"), 1, P("void"), 2,
            1, P("INDEX"), P("iFormat"), 0, 2, 0, P("pmc"), 0, P(&CS_pmcPostRead),
    /* 2 */ 1, 9, 2, P("mdPostChange"), 0, 0, 0, 0, 1, P("CCellShadedShaderArgs"), 1, P("void"), 2,
            1, P("CMetaPointer"), P("mpMember"), 0, 1, P("CMetaPointer"), P("mpObject"), 0, P(&CS_pmcPostChange),
    /* 3 */ 1, 9, 3, P("mdVerify"), 0, 0, 0, 0, 1, P("CCellShadedShaderArgs"), 1, P("void"), 2,
            1, P("CMetaPointer"), P("mp"), 0, 2, 0, P("pmc"), 0, P(&CS_pmcVerify),
    /* 4 */ 1, 5, 4, P("CCellShadedShaderArgs"), 1, sizeof(Args), 0, 0,
            1, P("CStandardShaderArgs"), P(""), P(""), 5,
            1, P("CShaderArgValCOLOR"), 2001, P("shade color"), P("sp_colCelShade"), P("2001"), P("Cel shading"),
               0x678, 0x40, 0, 0, 0,
            1, P("CShaderArgValFLOAT"), 2002, P("shade intensity"), P("sp_fCelShadeIntensity"), P("2002"),
               P("Cel shading"), 0x690, 0x40, 0, 0, 0,
            1, P("CShaderArgValFLOAT"), 2003, P("shadow sharpness"), P("sp_fCelSharpness"), P("2003"), P("Cel shading"),
               0x6a8, 0x40, 0, 0, 0,
            1, P("CShaderArgValFLOAT"), 2004, P("shadow threshold"), P("sp_fCelThreshold"), P("2004"), P("Cel shading"),
               0x6c0, 0x40, 0, 0, 0,
            1, P("CShaderArgValFLOAT"), 2005, P("ambient flatness"), P("sp_fCelFlatness"), P("2005"), P("Cel shading"),
               0x6d8, 0x40, 0, 0, 0,
            3, 2, 1, 2, 2, 2, 3,
            0,
            8, 0x200000, P(CS_Args_Construct), 0x400000, P(eng_meta_Destruct), 1, P(CS_Args_New),
               8, P(eng_meta_DynamicPointer), 2, P(CS_Args_Delete), 0x80, P(CS_Args_Fn80),
               0x800, P(eng_StandardShaderArgs_Fn800), 0x1000000, P(eng_StandardShaderArgs_Fn1000000),
               P(&CS_ptArgsType),
    /* 5 */ 1, 5, 5, P("CCellShadedShader"), 1, sizeof(Shader), 0, 0, 1, P("CStandardShader"), P(""), P(""), 0,
            0, 0,
            5, 0x200000, P(CS_Shader_Construct), 0x400000, P(eng_meta_Destruct), 1, P(CS_Shader_New),
               8, P(eng_meta_DynamicPointer), 2, P(eng_meta_DeleteObject), P(&CS_ptShaderType),
    /* the service object */
    2, P("_svc_CellShadedShader"), P(""), P(&CS_svc), 1, P("CService"), 0, 0,
    (u64)-1,
};

/* ------------------------------------------------------------------------------------------------ registration */
static const char k_strEmbeddedFile[] = "Sources/Shaders/CellShadedShader.psh";

/* once: the service's names, the embedded source (for a game without the file) and our types */
static void RegisterOnce(void) {
    if (CS_bRegistered) return;
    CS_bRegistered = 1;
    eng_Name_Set(&CS_svc.svc_strName, "Service.CellShadedShader");
    eng_Name_Set(&CS_svc.svc_strGroup, "Group.Shaders");
    eng_RegisterEmbeddedFile(k_strEmbeddedFile, k_strSource);
    eng_meta_register(CS_meta);
}

/* replaces the tail call at the end of the engine's shader registration: the engine's shaders first, then ours */
void CS_Register(void *pShaders) {
    eng_RegisterShadersTail(pShaders);
    RegisterOnce();
    u8 aType[16];
    MetaPtr mp;
    eng_meta_FindType(aType, "CCellShadedShader");
    eng_meta_New(&mp, aType);
    void *psh = mp.ptr;
    if (psh && mp.ptType && eng_ptShaderType && eng_meta_IsDerivedFrom(mp.ptType, eng_ptShaderType))
        eng_RegisterShader(pShaders, 0x799a, g_csName, psh);
    else
        eng_Warning("CellShaded shader: cannot create the shader object (%1).\n", 0xabcd0009, "CCellShadedShader");
}

/* at exit: the default arguments */
static void CS_DefaultArgs_Destroy(void) {
    if (!CS_bDefaultArgs) return;
    eng_ShaderArgsCache_Destruct(CS_acDefaultArgs);
    Args_Destruct(&CS_argsDefault);
    CS_bDefaultArgs = 0;
}
