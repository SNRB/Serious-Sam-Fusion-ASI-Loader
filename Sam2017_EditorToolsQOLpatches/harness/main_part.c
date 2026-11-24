/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *exePath = argc > 1 ? argv[1] : "Sam2017_SeriousEditor.exe";
    const char *pluginPath = argc > 2 ? argv[2] : "Sam2017_EditorToolsQOLpatches.asi";
    const char *mode = argc > 3 ? argv[3] : "full";
    setvbuf(stdout, 0, _IONBF, 0);

    struct sigaction sa = { 0 };
    sa.sa_sigaction = on_crash; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGILL, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGFPE, &sa, 0);

    load_editor(exePath);
    map_pristine();
    setup_teb();
    utf8_to_w("C:\\Games\\Serious Sam Fusion 2017\\Bin\\x64\\Sam2017_SeriousEditor.exe", g_exePathW, 512);
    utf8_to_w("C:\\Games\\Serious Sam Fusion 2017\\Bin\\x64\\plugins\\Sam2017_EditorToolsQOLpatches.asi", g_selfPathW, 512);
    if (strcmp(mode, "baseline")) g_plugin = load_plugin(pluginPath, &g_pluginSize);
    printf("editor mapped at %p, plugin at %p\n", (void *)g_exe, (void *)g_plugin);

    if (!strcmp(mode, "full")) {
        run_full();
    } else if (!strcmp(mode, "baseline")) {
        run_baseline();
    } else if (!strcmp(mode, "late")) {
        printf("[*] loaded after the program started (any time works: the slots are read when a tool starts)\n");
        *(u64 *)0x142A32688ULL = 0x1234567890ULL;            /* the CRT has set the /GS cookie */
        g_dllMain(g_plugin, 1, 0);
        CHECK(log_has("] applied to Sam2017_SeriousEditor.exe"), "applied");
        *(u64 *)0x142A32688ULL = *(u64 *)(g_pristine + 0x2A32688);
        CHECK(only_slots_changed() && in_plugin(*(u64 *)SLOT_REPARENT) && g_boxes == 0, "the two slots changed, no message box");
    } else if (!strcmp(mode, "twice")) {
        printf("[*] two copies of the plugin\n");
        g_dllMain(g_plugin, 1, (void *)1);
        u64 s1 = *(u64 *)SLOT_REPARENT, s2 = *(u64 *)SLOT_IN_PLACE;
        u32 sz2; u8 *second = load_plugin(pluginPath, &sz2);
        g_logLen = 0; g_logText[0] = 0;
        g_dllMain(second, 1, (void *)1);
        CHECK(log_has("already applied by another copy of this plugin, skipped"), "second copy skips (environment variable)");
        CHECK(*(u64 *)SLOT_REPARENT == s1 && *(u64 *)SLOT_IN_PLACE == s2 && g_boxes == 0, "the first copy's slots kept, no message box");
    } else if (!strcmp(mode, "conflict")) {
        printf("[*] another mod already replaced the Reparent tool's slot\n");
        *(u64 *)SLOT_REPARENT = 0x140001000ULL;
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("NOT APPLIED: The program data it changes was already changed by something else (at 0x142622060)"),
              "conflict reported with its address");
        CHECK(g_boxes == 1, "user told with a message box");
        *(u64 *)SLOT_REPARENT = ORIG_REPARENT;
        CHECK(image_diff(0) == 0 && g_nprot == 0, "nothing changed");
    } else if (!strcmp(mode, "relied")) {
        printf("[*] code the plugin relies on was changed (the editor's Reparent)\n");
        *(u8 *)0x1419EE100ULL ^= 1;
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("NOT APPLIED: The program code or data it relies on is different from what it expects (at 0x1419ee000)"),
              "reported with the region address");
        *(u8 *)0x1419EE100ULL ^= 1;
        CHECK(image_diff(0) == 0 && g_nprot == 0, "nothing changed");
    } else if (!strcmp(mode, "game")) {
        printf("[*] loaded by the game, Sam2017_Unrestricted.exe\n");
        utf8_to_w("C:\\Games\\Serious Sam Fusion 2017\\Bin\\x64\\Sam2017_Unrestricted.exe", g_exePathW, 512);
        *(u32 *)(g_exe + pe_nt(g_exe) + 8) = 0x5CF9A1F6;
        *(u32 *)(g_exe + pe_nt(g_exe) + 24 + 56) = 0x027FB000;
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("nothing to do: Sam2017_Unrestricted.exe (TimeDateStamp 0x5cf9a1f6, SizeOfImage 0x27fb000) is not a program this plugin knows"),
              "logs 'nothing to do' for the game");
        CHECK(g_boxes == 0 && g_nprot == 0, "no message box, nothing changed");
    } else if (!strcmp(mode, "wrongbuild")) {
        printf("[*] a different build of Sam2017_SeriousEditor.exe\n");
        *(u32 *)(g_exe + pe_nt(g_exe) + 8) = 0x5D000000;
        g_dllMain(g_plugin, 1, (void *)1);
        CHECK(log_has("is not one of the original executables (builds of 7 June 2019 and 19 November 2026)"), "logged");
        CHECK(g_boxes == 1, "warning box shown");
        CHECK(g_nprot == 0, "nothing changed");
    } else {
        printf("unknown mode %s\n", mode);
        return 2;
    }
    if (g_logLen) printf("  plugin log:\n%s", g_logText);
    printf("%s: %d failure(s)\n", mode, g_failures);
    return g_failures ? 1 : 0;
}
