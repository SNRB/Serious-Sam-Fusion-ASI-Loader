/* ------------------------------------------------------------------ */
/* Sam2017_EditorToolsQOLpatches tests                                 */
/* ------------------------------------------------------------------ */

typedef MSABI void (*fn_void)(void);
typedef MSABI void *(*fn_ptr)(void);
typedef MSABI void *(*fn_ptr_ptr)(void *);
typedef MSABI void (*fn_void_ptr)(void *);
typedef MSABI void (*fn_void_ptr_ptr_int)(void *, void *, int);
typedef MSABI void *(*fn_alloc)(size_t, void *);
typedef MSABI u64 *(*fn_ident)(u64 *out, const char *s);
typedef MSABI const char *(*fn_id_text)(u64 id);
typedef MSABI void *(*fn_scope_begin)(void *scope, void *tool);
typedef MSABI void (*fn_scope_commit)(void *scope, const char *name, int flag);
typedef MSABI void (*fn_scope_end)(void *scope);
typedef MSABI int (*fn_tool_exec)(void *tool);
typedef MSABI int (*fn_int_ptr)(void *);
typedef MSABI int (*fn_int_int)(int);
typedef MSABI void (*fn_void_int)(int);
typedef MSABI void (*fn_void_int_u64)(int, u64);
typedef MSABI void (*fn_void_int_ptr)(int, const void *);
typedef MSABI void (*fn_void_int_float)(int, float);
typedef MSABI void *(*fn_ptr_ptr_ptr)(void *, void *);
typedef MSABI float *(*fn_placement)(float *out, int bone, int flags);
typedef MSABI u64 *(*fn_bone_ident)(u64 *out, int bone);
typedef MSABI void *(*fn_ptr_int)(int);

#define SLOT_REPARENT     0x142622060ULL     /* vtable 0x142621F70 (Tool.SetBoneParent), slot 30 */
#define SLOT_IN_PLACE     0x142622180ULL     /* vtable 0x142622090 (Tool.SetBoneParentInPlace), slot 30 */
#define ORIG_REPARENT     0x1419EDFF0ULL
#define ORIG_IN_PLACE     0x1419EDFE0ULL
#define FACTORY_REPARENT  0x1419EF2C0ULL
#define FACTORY_IN_PLACE  0x1419EF280ULL
#define TREE_GLOBAL       0x143312D80ULL     /* the skeleton tree view (0x1419EC460 returns it) */

static int in_plugin(u64 a) { return a >= (u64)g_plugin && a < (u64)g_plugin + g_pluginSize; }
static int log_has(const char *s) { return strstr(g_logText, s) != 0; }
static u64 ident(const char *s) { u64 id = 0; ((fn_ident)0x140B2AB30ULL)(&id, s); return id; }
static const char *id_text(u64 id) { const char *s = ((fn_id_text)0x140B2A9F0ULL)(id); return s ? s : ""; }
static void *eng_alloc(size_t n) { void *p = ((fn_alloc)0x140B20EF0ULL)(n, 0); memset(p, 0, n); return p; }

/* the program image as loaded, before the plugin ran */
static u8 *g_pristine;
static void map_pristine(void) { g_pristine = malloc(g_exeSize); memcpy(g_pristine, g_exe, g_exeSize); }
static long image_diff(u64 *first)
{
    long n = 0;
    for (u64 i = 0; i < g_exeSize; i++) {
        if (g_exe[i] == g_pristine[i]) continue;
        if (!n && first) *first = EXE_BASE + i;
        n++;
    }
    return n;
}
/* 1 when every byte that differs from the loaded image is inside one of the two slots */
static int only_slots_changed(void)
{
    for (u64 i = 0; i < g_exeSize; i++) {
        if (g_exe[i] == g_pristine[i]) continue;
        u64 a = EXE_BASE + i;
        if (!((a >= SLOT_REPARENT && a < SLOT_REPARENT + 8) || (a >= SLOT_IN_PLACE && a < SLOT_IN_PLACE + 8))) {
            printf("      unexpected change at %#lx\n", (unsigned long)a);
            return 0;
        }
    }
    return 1;
}

/* ---- the editor's runtime --------------------------------------------------------------------- */
static int g_initIndex;
MSABI static u64 ret0_fn(void) { return 0; }
static void run_static_initializers(u64 from, u64 to)
{
    for (u64 a = from; a < to; a += 8) {
        u64 fn = *(u64 *)a;
        g_initIndex = (int)((a - from) / 8);
        if (!fn) continue;
        int r = sigsetjmp(g_initJmp, 1);
        if (r == 0) { g_inInit = 1; ((fn_void)fn)(); g_inInit = 0; }
        else g_initSkipped++;
    }
}
static void start_editor_runtime(void)
{
    install_memory_standins();
    /* the C runtime's table of optional Windows functions (stored XOR the /GS cookie): all "absent" */
    for (u64 a = 0x14331FF70ULL; a < 0x143320098ULL; a += 8) if (!*(u64 *)a) *(u64 *)a = *(u64 *)0x142A32688ULL;
    write_jmp(0x141ABA8B0ULL, (void *)ret0_fn);           /* atexit: destructors are not needed here */
    run_static_initializers(0x141B54CE0ULL, 0x141B77A00ULL);
    ((fn_void)0x140B2BF50ULL)();                           /* the engine's object ID tables */
    /* the editor's Reparent keeps IDENT("HintList.Bones") in a function-local static (0x1433134E0) made
       on first use under the C runtime's thread-safe statics, whose wait functions are not set up here:
       give it its value, with its guard at the thread's epoch (INT_MIN, from the TLS template) */
    *(u64 *)0x1433134E0ULL = ident("HintList.Bones");
    *(u32 *)0x1433134E8ULL = *(u32 *)0x143524068ULL;
}

/* ---- the document's undo, tools ----------------------------------------------------------------- */
static u8 *g_holder;
static void *make_tool_with_journal(u64 factory)
{
    void *tool = ((fn_ptr)factory)();
    u8 *j = eng_alloc(0x20);
    ((fn_ptr_ptr)0x1415E2DF0ULL)(j);
    *(void **)((u8 *)tool + 0x30) = j;
    ((fn_void_ptr_ptr_int)0x1415E2ED0ULL)(j, tool, 0);
    return tool;
}
/* a tool run as the editor's tool activation runs it: made by its factory, with its journal, slot 30 */
static int run_tool(u64 factory)
{
    void *tool = make_tool_with_journal(factory);
    return ((fn_tool_exec)(*(u64 **)tool)[30])(tool);
}
static void undo(void) { ((void (MSABI *)(int))0x141645190ULL)(0); }
static void redo(void) { ((void (MSABI *)(int))0x141645010ULL)(0); }

/* ---- the skeleton, made with the skeleton editor's own functions ------------------------------- */
typedef struct { float qx, qy, qz, qw, vx, vy, vz; } QV;
static u8 *g_eskel, *g_lod;
static int g_hLod;
static const struct { const char *name, *parent; double x, y, z, h, p, b; } k_bones[] = {
    { "Root",   "",       0, 0, 0 },
    { "Pelvis", "Root",   0, 1.0, 0,       10, 0, 0 },
    { "Spine",  "Pelvis", 0, 0.2, 0.02,    0, 5, 0 },
    { "Chest",  "Spine",  0, 0.25, 0,      0, 0, -3 },
    { "Neck",   "Chest",  0, 0.2, 0.01 },
    { "Head",   "Neck",   0, 0.12, 0,      15, 0, 0 },
    { "ArmL",   "Chest",  0.2, 0.15, 0,    0, 0, 80 },
    { "HandL",  "ArmL",   0, 0.5, 0,       0, 20, 0 },
    { "ArmR",   "Chest",  -0.2, 0.15, 0,   0, 0, -80 },
    { "HandR",  "ArmR",   0, 0.5, 0 },
    { "LegL",   "Pelvis", 0.1, -0.1, 0,    5, -10, 0 },
    { "LegR",   "Pelvis", -0.1, -0.1, 0,   -5, -10, 0 },
    { "Prop",   "",       0.5, 1.2, 0.3,   30, 20, 10 },
};
#define NB ((int)(sizeof k_bones / sizeof k_bones[0]))

/* quaternion of heading, pitch, banking (degrees), as the editor's HPB: Qy(h) Qx(p) Qz(b) */
static void q_hpb(double h, double p, double b, float *q)
{
    const double D = 3.14159265358979323846 / 360;
    double ch = cos(h * D), sh = sin(h * D), cp = cos(p * D), sp = sin(p * D), cb = cos(b * D), sb = sin(b * D);
    /* Qy(h) * Qx(p) */
    double x = ch * sp, y = sh * cp, z = -sh * sp, w = ch * cp;
    /* * Qz(b) */
    q[0] = (float)(x * cb + y * sb); q[1] = (float)(y * cb - x * sb); q[2] = (float)(z * cb + w * sb); q[3] = (float)(w * cb - z * sb);
}

static int bone_handle(const char *name)
{
    int n = *(int *)(g_lod + 0x30);
    u8 **bones = *(u8 ***)(g_lod + 0x28);
    for (int i = 0; i < n; i++)
        if (*(u64 *)bones[i] == ident(name)) return ((fn_int_ptr)0x1419DC1B0ULL)(bones[i]);
    return -1;
}
static u8 *bone_obj(int h) { return ((fn_ptr_int)0x1419DC1E0ULL)(h); }
static const char *bone_name(int h) { u64 id = 0; return id_text(*((fn_bone_ident)0x1419D2330ULL)(&id, h)); }
static const char *parent_name(const char *b) { u64 id = 0; return id_text(*((fn_bone_ident)0x1419D23F0ULL)(&id, bone_handle(b))); }
/* the parent bone the editor finds for a bone (its own lookup by name), "" for none */
static const char *parent_bone(const char *b)
{
    int p = ((fn_int_int)0x1419F5730ULL)(bone_handle(b));
    return p == -1 ? "" : bone_name(p);
}
static QV placement(const char *b) { QV r; ((fn_placement)0x1419F5630ULL)((float *)&r, bone_handle(b), 0); return r; }
static QV rel_placement(const char *b) { QV r; memcpy(&r, bone_obj(bone_handle(b)) + 0x40, sizeof r); return r; }
static double qv_diff(QV a, QV b)
{
    double s = a.qx * b.qx + a.qy * b.qy + a.qz * b.qz + a.qw * b.qw, sg = s < 0 ? -1 : 1, m = 0;
    double d[7] = { a.qx - sg * b.qx, a.qy - sg * b.qy, a.qz - sg * b.qz, a.qw - sg * b.qw, a.vx - b.vx, a.vy - b.vy, a.vz - b.vz };
    for (int i = 0; i < 7; i++) if (fabs(d[i]) > m) m = fabs(d[i]);
    return m;
}

static void build_skeleton(void)
{
    g_eskel = eng_alloc(0x48);
    ((fn_ptr_ptr)0x1419DB530ULL)(g_eskel);                          /* CEditSkeleton */
    g_lod = ((fn_ptr_ptr_ptr)0x1419DBC70ULL)(g_eskel, 0);            /* CEditSkeleton: new LOD */
    g_hLod = ((fn_int_ptr)0x1419DC2E0ULL)(g_lod);
    void *tool = make_tool_with_journal(FACTORY_REPARENT);
    void *scope[4] = { 0 };
    ((fn_scope_begin)0x1415C0510ULL)(scope, tool);
    for (int i = 0; i < NB; i++) {
        int h = ((fn_int_int)0x1419D1B30ULL)(g_hLod);               /* create bone (undoable) */
        ((fn_void_int_u64)0x1419D2760ULL)(h, ident(k_bones[i].name));
        if (k_bones[i].parent[0]) ((fn_void_int_u64)0x1419D2780ULL)(h, ident(k_bones[i].parent));
        QV r;
        q_hpb(k_bones[i].h, k_bones[i].p, k_bones[i].b, &r.qx);
        r.vx = (float)k_bones[i].x; r.vy = (float)k_bones[i].y; r.vz = (float)k_bones[i].z;
        ((fn_void_int_ptr)0x1419D27A0ULL)(h, &r);
    }
    ((fn_scope_commit)0x1415C0680ULL)(scope, "harness skeleton", 1);
    ((fn_scope_end)0x1415C05B0ULL)(scope);
}

/* the whole skeleton: each bone's parent name, parent bone and both placements */
typedef struct { char pname[64], pbone[64]; QV rel, abs; } BoneState;
static void snapshot(BoneState *s)
{
    for (int i = 0; i < NB; i++) {
        snprintf(s[i].pname, 64, "%s", parent_name(k_bones[i].name));
        snprintf(s[i].pbone, 64, "%s", parent_bone(k_bones[i].name));
        s[i].rel = rel_placement(k_bones[i].name);
        s[i].abs = placement(k_bones[i].name);
    }
}
static int bone_index(const char *n) { for (int i = 0; i < NB; i++) if (!strcmp(k_bones[i].name, n)) return i; return -1; }
/* same skeleton: parent names, parent bones and relative placements exactly */
static int same_exactly(const BoneState *a, const BoneState *b)
{
    for (int i = 0; i < NB; i++)
        if (strcmp(a[i].pname, b[i].pname) || strcmp(a[i].pbone, b[i].pbone) || memcmp(&a[i].rel, &b[i].rel, sizeof a[i].rel)) {
            printf("      %s differs\n", k_bones[i].name);
            return 0;
        }
    return 1;
}
/* largest change of a bone's placement in the model */
static double moved(const BoneState *a, const BoneState *b)
{
    double m = 0;
    for (int i = 0; i < NB; i++) { double d = qv_diff(a[i].abs, b[i].abs); if (d > m) m = d; }
    return m;
}
/* every parent chain ends at a root */
static int no_loops(void)
{
    for (int i = 0; i < NB; i++) {
        int h = bone_handle(k_bones[i].name), k = 0;
        while (h != -1 && k++ <= NB) h = ((fn_int_int)0x1419F5730ULL)(h);
        if (h != -1) return 0;
    }
    return 1;
}

/* ---- the skeleton editor around the tools ------------------------------------------------------ */
/* a stand-in skeleton editor with the editor's own bone selection at +0x3A0 (vtable 0x14261A2E8,
   built as the skeleton editor's constructor 0x1419D29D0 builds it); its item source (+0x1C0 =
   editor +0x560) gives the LOD's bones: index -> bone (slot 4), bone -> index (slot 6), count (8) */
static u8 *g_skelEd;
static void *g_dlgVt[16], *g_dlg[2];
static int lod_count(void) { return *(int *)(g_lod + 0x30); }
static u8 *lod_bone(int i) { return (*(u8 ***)(g_lod + 0x28))[i]; }
MSABI static void *src_item(void *self, int i) { (void)self; return i >= 0 && i < lod_count() ? lod_bone(i) : 0; }
MSABI static int src_index(void *self, void *b) { (void)self; for (int i = 0; i < lod_count(); i++) if (lod_bone(i) == b) return i; return -1; }
MSABI static int src_count(void *self) { (void)self; return lod_count(); }
MSABI static void src_unexpected(void)
{
    printf("TRAP: a selection item source slot that is not emulated, called from %p\n", __builtin_return_address(0));
    fflush(stdout); _exit(3);
}
MSABI static void *fake_SkeletonEditor(void) { return g_skelEd; }
static u8 *selection(void) { return g_skelEd + 0x3A0; }
static void setup_skeleton_editor(void)
{
    g_skelEd = eng_alloc(0x800);
    u8 *sel = selection();
    ((fn_ptr_ptr)0x1416E0230ULL)(sel);                /* the selection's base constructor */
    *(u64 *)sel = 0x14261A2E8ULL;                      /* the skeleton editor's bone selection */
    *(int *)(sel + 0x1A0) = 0x10;
    *(int *)(sel + 0x1B8) = 0x10;
    for (int i = 0; i < 16; i++) g_dlgVt[i] = (void *)src_unexpected;
    g_dlgVt[4] = (void *)src_item; g_dlgVt[6] = (void *)src_index; g_dlgVt[8] = (void *)src_count;
    g_dlg[0] = g_dlgVt;
    *(void **)(sel + 0x1C0) = g_dlg;
    write_jmp(0x1419DA450ULL, (void *)fake_SkeletonEditor);      /* the active skeleton editor */
    *(u8 **)TREE_GLOBAL = eng_alloc(0x100);                       /* the skeleton tree view */
}
/* the editor's own selecting (0x1419DA5F0) and deselecting (selection slot 14) */
static void select_bones(const char *const *names, int n)
{
    u8 *sel = selection();
    while (*(int *)(sel + 0x198) > 0)
        ((void (MSABI *)(void *, int))(*(u64 **)sel)[14])(sel, src_index(0, (*(void ***)(sel + 0x190))[0]));
    for (int i = 0; i < n; i++) ((fn_void_int)0x1419DA5F0ULL)(bone_handle(names[i]));
}
/* the selection as the tools get it (0x1419D9DB0), as text */
static void selected_text(char *out, size_t cap)
{
    int list = -1;
    ((int *(MSABI *)(int *))0x1419D9DB0ULL)(&list);
    int n = ((int (MSABI *)(const int *))0x1415BFD00ULL)(&list);
    size_t len = 0; out[0] = 0;
    for (int i = 0; i < n; i++)
        len += snprintf(out + len, cap - len, "%s%s", i ? " " : "", bone_name(((int (MSABI *)(const int *, int))0x1415BFD30ULL)(&list, i)));
    ((fn_void_int)0x1415BFE40ULL)(list);
}

/* the dialog (0x141673DB0): records what it was asked, answers g_answer (or Cancel) */
static struct { int calls; char title[256], text[1024], def[256], hints[64]; } g_ask;
static const char *g_answer = "";
static int g_answerOk = 1;
MSABI static int fake_AskText(const u64 *title, const u64 *text, u64 *value, int a, int b, u64 hints, int c)
{
    (void)a; (void)b; (void)c;
    g_ask.calls++;
    snprintf(g_ask.title, sizeof g_ask.title, "%s", (const char *)*title);
    snprintf(g_ask.text, sizeof g_ask.text, "%s", (const char *)*text);
    snprintf(g_ask.def, sizeof g_ask.def, "%s", (const char *)*value);
    snprintf(g_ask.hints, sizeof g_ask.hints, "%s", id_text(hints));
    printf("    dialog \"%s\": \"%s\" [%s] (hints %s) -> %s\n", g_ask.title, g_ask.text, g_ask.def, g_ask.hints,
           g_answerOk ? g_answer : "Cancel");
    if (!g_answerOk) return 0;
    ((fn_void_ptr)0x140B269A0ULL)(value);                         /* the editor's CString: free, then set */
    ((u64 *(MSABI *)(u64 *, const char *))0x140B25C50ULL)(value, g_answer);
    return 1;
}
static int ask(const char *answer, u64 factory, const char *const *sel, int n)
{
    select_bones(sel, n);
    memset(&g_ask, 0, sizeof g_ask);
    g_answer = answer; g_answerOk = 1;
    edlog_clear();
    return run_tool(factory);
}

/* ---- tests ------------------------------------------------------------------------------------- */
static void test_install(void)
{
    printf("[1] install\n");
    g_dllMain(g_plugin, 1, (void *)1);
    CHECK(log_has("] applied to Sam2017_SeriousEditor.exe (vtable slots 0x142622060 and 0x142622180): Skeleton editor "
                  "\"Reparent\" (Shift+S) and \"Reparent in place\" (Shift+Alt+S) give the new parent to every selected bone"),
          "logged");
    CHECK(g_boxes == 0, "no message box");
    CHECK(in_plugin(*(u64 *)SLOT_REPARENT) && in_plugin(*(u64 *)SLOT_IN_PLACE) && *(u64 *)SLOT_REPARENT != *(u64 *)SLOT_IN_PLACE,
          "slot 30 of both tools' vtables points to the plugin");
    CHECK(only_slots_changed(), "nothing else in the program changed (%ld bytes, all in the two slots)", image_diff(0));
    int wr = 0;
    for (int i = 0; i < g_nprot; i++) if (g_prot[i].prot == 0x04 && g_prot[i].addr == SLOT_REPARENT) wr++;
    CHECK(wr == 1 && g_nprot >= 2 && g_prot[g_nprot - 1].prot == 0x02, "made writable for the change and read-only again");
    CHECK(g_pinned, "the plugin pinned itself (the slots point to it)");
}

static BoneState g_orig[NB];
static void test_skeleton(void)
{
    printf("[2] a skeleton made with the skeleton editor's own undoable functions (one undo step)\n");
    build_skeleton();
    setup_skeleton_editor();
    snapshot(g_orig);
    int ok = lod_count() == NB;
    for (int i = 0; i < NB; i++) if (strcmp(g_orig[i].pbone, k_bones[i].parent)) ok = 0;
    CHECK(ok, "%d bones in one LOD, each under the parent it was given (found by the editor's own lookup)", lod_count());
    const char *order[] = { "ArmR", "Head", "ArmL" };
    select_bones(order, 3);
    char t[256]; selected_text(t, sizeof t);
    CHECK(!strcmp(t, "ArmR Head ArmL"), "the editor's selection lists the bones in the order they were selected: \"%s\"", t);
}

static QV g_refArmL;                 /* the editor's own result for ArmL under Spine */
static void test_one_bone(void)
{
    printf("[3] one bone selected: the editor's own Reparent\n");
    const char *sel[] = { "ArmL" };
    BoneState before[NB], after[NB];
    snapshot(before);
    ask("Spine", FACTORY_REPARENT, sel, 1);
    snapshot(after);
    CHECK(g_ask.calls == 1 && !strcmp(g_ask.title, "Reparent bone") && !strcmp(g_ask.text, "Choose new parent for 'ArmL' bone") &&
          !strcmp(g_ask.def, "Chest") && !strcmp(g_ask.hints, "HintList.Bones"),
          "its dialog: \"Reparent bone\", \"Choose new parent for 'ArmL' bone\", starting with its parent Chest");
    int a = bone_index("ArmL");
    CHECK(!strcmp(after[a].pbone, "Spine") && moved(before, after) < 2e-6, "ArmL is under Spine, kept in place (%.1e)",
          moved(before, after));
    g_refArmL = after[a].rel;
    undo();
    snapshot(after);
    CHECK(same_exactly(after, before), "undo: as before");
}

static void test_several(void)
{
    printf("[4] three bones selected (ArmL, ArmR, Head): all get the new parent\n");
    const char *sel[] = { "ArmL", "ArmR", "Head" };
    BoneState before[NB], after[NB], redone[NB];
    snapshot(before);
    *(int *)(*(u8 **)TREE_GLOBAL + 0x78) = 7;
    ask("Spine", FACTORY_REPARENT, sel, 3);
    snapshot(after);
    CHECK(g_ask.calls == 1 && !strcmp(g_ask.title, "Reparent bones") &&
          !strcmp(g_ask.text, "Choose new parent for the 3 selected bones: 'ArmL', 'ArmR' and 'Head'") &&
          !strcmp(g_ask.def, "Chest") && !strcmp(g_ask.hints, "HintList.Bones"),
          "one dialog: \"Reparent bones\", \"%s\", starting with the first bone's parent", g_ask.text);
    int okp = 1;
    for (int i = 0; i < 3; i++) {
        int k = bone_index(sel[i]);
        if (strcmp(after[k].pbone, "Spine") || strcmp(after[k].pname, "Spine")) okp = 0;
    }
    CHECK(okp, "ArmL, ArmR and Head are under Spine (parent name and parent bone)");
    double mv = moved(before, after);
    CHECK(mv < 2e-6, "kept in place: no bone moved in the model, HandL and HandR included (%.1e)", mv);
    int others = 1;
    for (int i = 0; i < NB; i++) {
        if (!strcmp(k_bones[i].name, "ArmL") || !strcmp(k_bones[i].name, "ArmR") || !strcmp(k_bones[i].name, "Head")) continue;
        if (strcmp(after[i].pname, before[i].pname) || memcmp(&after[i].rel, &before[i].rel, sizeof(QV))) others = 0;
    }
    CHECK(others, "the other bones are exactly as they were");
    double dr = qv_diff(after[bone_index("ArmL")].rel, g_refArmL);
    CHECK(dr < 1e-6, "ArmL got the placement the editor's own Reparent gives it under Spine (%.1e)", dr);
    CHECK(edlog_has("Reparent bones: 'ArmL', 'ArmR' and 'Head' are now under 'Spine' (kept in place).\n"),
          "console: \"Reparent bones: 'ArmL', 'ArmR' and 'Head' are now under 'Spine' (kept in place).\"");
    CHECK(*(int *)(*(u8 **)TREE_GLOBAL + 0x78) == 0, "the skeleton tree was told to rebuild");
    undo();
    snapshot(redone);
    CHECK(same_exactly(redone, before), "one undo step: all three back under their parents, placements exactly as before");
    redo();
    snapshot(redone);
    CHECK(same_exactly(redone, after), "redo: all three under Spine again");
    undo();
}

static void test_new_parent_selected(void)
{
    printf("[5] the new parent is one of the selected bones\n");
    const char *sel[] = { "Neck", "ArmL", "ArmR" };
    BoneState before[NB], after[NB];
    snapshot(before);
    ask("Neck", FACTORY_REPARENT, sel, 3);
    snapshot(after);
    CHECK(!strcmp(after[bone_index("ArmL")].pbone, "Neck") && !strcmp(after[bone_index("ArmR")].pbone, "Neck") &&
          !strcmp(after[bone_index("Neck")].pbone, "Chest") && moved(before, after) < 2e-6,
          "ArmL and ArmR under Neck, Neck keeps its parent Chest, nothing moved");
    CHECK(edlog_has("Reparent bones: 'ArmL' and 'ArmR' are now under 'Neck' (kept in place). Not changed: 'Neck' (it is the new parent).\n"),
          "console says Neck was left as it is");
    undo();
    snapshot(after);
    CHECK(same_exactly(after, before), "undo");
}

static void test_loop(void)
{
    printf("[6] a selected bone above the new parent (it would make a loop)\n");
    const char *sel[] = { "Spine", "Prop" };
    BoneState before[NB], after[NB];
    snapshot(before);
    ask("Head", FACTORY_REPARENT, sel, 2);
    snapshot(after);
    CHECK(!strcmp(after[bone_index("Prop")].pbone, "Head") && !strcmp(after[bone_index("Spine")].pbone, "Pelvis") &&
          moved(before, after) < 2e-6 && no_loops(), "Prop under Head; Spine (Head hangs below it) keeps its parent; no loop");
    CHECK(edlog_has("Reparent bones: 'Prop' is now under 'Head' (kept in place). Not changed: 'Spine' (the new parent hangs below "
                    "it, so that would make a loop).\n"), "console says why Spine was left");
    undo();
    snapshot(after);
    CHECK(same_exactly(after, before), "undo");

    const char *sel2[] = { "Spine", "Chest" };
    ask("Head", FACTORY_REPARENT, sel2, 2);
    snapshot(after);
    CHECK(same_exactly(after, before) && edlog_has("[16]Reparent bones - nothing done: 'Spine' and 'Chest' (the new parent hangs "
                                                    "below them, so that would make a loop).\n"),
          "both above the new parent: nothing done, a warning in the console");
}

static void test_no_parent(void)
{
    printf("[7] an empty name, a name no bone has\n");
    const char *sel[] = { "HandL", "HandR" };
    BoneState before[NB], after[NB];
    snapshot(before);
    ask("", FACTORY_REPARENT, sel, 2);
    snapshot(after);
    CHECK(!after[bone_index("HandL")].pbone[0] && !after[bone_index("HandR")].pbone[0] && !after[bone_index("HandL")].pname[0] &&
          moved(before, after) < 2e-6, "empty: HandL and HandR are roots, kept in place");
    CHECK(edlog_has("Reparent bones: 'HandL' and 'HandR' have no parent now (kept in place).\n"), "console says so");
    undo();
    snapshot(after);
    CHECK(same_exactly(after, before), "undo");
    const char *sel2[] = { "LegL", "LegR" };
    ask("Tail", FACTORY_REPARENT, sel2, 2);
    snapshot(after);
    CHECK(!strcmp(after[bone_index("LegL")].pname, "Tail") && !after[bone_index("LegL")].pbone[0] &&
          !strcmp(after[bone_index("LegR")].pname, "Tail") && moved(before, after) < 2e-6,
          "\"Tail\": parent name Tail, no parent bone, kept in place (as the editor does for one bone)");
    CHECK(edlog_has("Reparent bones: 'LegL' and 'LegR' are now under 'Tail' (kept in place); no bone of their skeleton LOD is "
                    "named 'Tail', so they have that parent name but no parent bone.\n"), "console says no bone is named Tail");
    undo();
    snapshot(after);
    CHECK(same_exactly(after, before), "undo");
}

static void test_cancel_and_none(void)
{
    printf("[8] Cancel, nothing selected\n");
    const char *sel[] = { "ArmL", "ArmR" };
    BoneState before[NB], after[NB];
    snapshot(before);
    select_bones(sel, 2);
    memset(&g_ask, 0, sizeof g_ask);
    g_answerOk = 0;
    edlog_clear();
    run_tool(FACTORY_REPARENT);
    g_answerOk = 1;
    snapshot(after);
    CHECK(g_ask.calls == 1 && same_exactly(after, before) && !edlog_has("Reparent"), "Cancel: nothing changes, nothing said");
    select_bones(sel, 0);
    memset(&g_ask, 0, sizeof g_ask);
    edlog_clear();
    run_tool(FACTORY_REPARENT);
    CHECK(g_ask.calls == 0 && edlog_has("No selected bones to change parent."), "nothing selected: the editor's own message");
}

static void test_in_place(void)
{
    printf("[9] \"Reparent in place\" (Tool.SetBoneParentInPlace, Shift+Alt+S) with three bones\n");
    const char *sel[] = { "LegL", "LegR", "Prop" };
    BoneState before[NB], after[NB];
    snapshot(before);
    ask("Chest", FACTORY_IN_PLACE, sel, 3);
    snapshot(after);
    CHECK(g_ask.calls == 1 && !strcmp(after[bone_index("LegL")].pbone, "Chest") && !strcmp(after[bone_index("LegR")].pbone, "Chest") &&
          !strcmp(after[bone_index("Prop")].pbone, "Chest") && moved(before, after) < 2e-6,
          "LegL, LegR and Prop under Chest, kept in place");
    undo();
    snapshot(after);
    CHECK(same_exactly(after, before), "undo");
    const char *one[] = { "Prop" };
    ask("Head", FACTORY_IN_PLACE, one, 1);
    CHECK(!strcmp(g_ask.title, "Reparent bone") && !strcmp(parent_bone("Prop"), "Head"), "one bone: the editor's own (\"Reparent bone\")");
    undo();
}

/* without the plugin: only the first selected bone changes */
static void run_baseline(void)
{
    printf("[*] without the plugin, three bones selected (ArmL, ArmR, Head)\n");
    start_editor_runtime();
    g_holder = ((fn_ptr)0x1417C5AE0ULL)();
    ((fn_void_ptr)0x1417C5C60ULL)(g_holder);
    write_jmp(0x141673DB0ULL, (void *)fake_AskText);
    build_skeleton();
    setup_skeleton_editor();
    const char *sel[] = { "ArmL", "ArmR", "Head" };
    ask("Spine", FACTORY_REPARENT, sel, 3);
    CHECK(!strcmp(g_ask.text, "Choose new parent for 'ArmL' bone") && !strcmp(parent_bone("ArmL"), "Spine") &&
          !strcmp(parent_bone("ArmR"), "Chest") && !strcmp(parent_bone("Head"), "Neck"),
          "the editor asks for ArmL only, and only ArmL gets the new parent");
}

static void run_full(void)
{
    test_install();
    start_editor_runtime();
    g_holder = ((fn_ptr)0x1417C5AE0ULL)();
    ((fn_void_ptr)0x1417C5C60ULL)(g_holder);                 /* the document's journal, as when it is active */
    write_jmp(0x141673DB0ULL, (void *)fake_AskText);
    test_skeleton();
    test_one_bone();
    test_several();
    test_new_parent_selected();
    test_loop();
    test_no_parent();
    test_cancel_and_none();
    test_in_place();
}
