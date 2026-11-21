# Sam2017_MoreConsoleCommandsSED.asi

ASI plugin for Serious Editor of Serious Sam Fusion 2017 (`Sam2017_SeriousEditor.exe`, build of 7 June 2019).
It declares console variables whose code is in the editor but which its console does not know, and adds a key-bound command.

## Console variables

| Console variable                | Address     | Type             | Saved to SeriousEditor.ini |
|---------------------------------|-------------|------------------|----------------------------|
| `hud_bShowGeometryInfo`         | 0x142AB9874 | BOOL             | no                         |
| `res_bSaveInTextFormat`         | 0x141EFB2D8 | INDEX (0, 1, 2)  | yes                        |
| `scr_bUnsecureScripting`        | 0x142A72FD8 | BOOL             | no                         |
| `prj_bUnlockAutoShotgunScope`   | 0x143279248 | BOOL             | no                         |
| `men_bMenuFrame`                | 0x143289A70 | BOOL             | no                         |
| `prj_bAllowLevelChangeInEditor` | 0x143278EAC | BOOL             | yes                        |
| `fxd_bShowDebug`                | 0x142AC5C50 | BOOL             | no                         |
| `fil_bReportFileOpen`           | 0x1431DE4C8 | BOOL             | no                         |
| `md_bReportReadStats`           | 0x1431DE960 | BOOL             | yes                        |

All of them default to 0.

- `res_bSaveInTextFormat` is a number because that is how the editor reads it: `0` = compressed binary (default),
  `1` = text, except resource types that are normally kept binary, `2` = text for all resources. The console clamps
  values outside 0-2.
- `md_bReportReadStats` prints part of its report as a **Trace** console message, so the Trace type must not be
  filtered out in the console to see it. The plugin does not change the console filter.

### Descriptions

The console search tooltip and `help <name>` show the editor's own description of each variable, from the help files
it loads (`Help/Cvars/*_cvars.xml`). A variable that has no description there, or no entry at all, shows
`(undocumented)`, as in the editor's help window. The first line of the tooltip (saved or not, type) always matches
how the plugin declares the variable, for example `saved pure INDEX res_bSaveInTextFormat`. The log file lists, for
each variable, whether its description came from the help files.

### Saving to SeriousEditor.ini

The three saved variables are written to `SeriousEditor.ini` by the editor itself when it closes, like its own saved
settings, as lines such as `res_bSaveInTextFormat = 2;`. The editor reads them back when it starts. A session that
ends in a crash saves nothing, as with the built-in settings. If the plugin is removed later, the editor keeps those
lines in the file and ignores them.

## Command: Toggle metadata codenames (Ctrl+Shift+Alt+C)

It appears as `Toggle metadata codenames` in Help > Commands list. It is registered in `CommandHandler.Global`, where
the editor registers its own tool commands, such as `Command.GenerateAssetUID` (Ctrl+Alt+U).

Each press flips the byte at 0x140B730F3 between 0x18 and 0x20. That byte is in the function that returns the display
name of a metadata member (`mov rcx,[rcx+18h]`). With 0x20 the function returns the member's code name instead, and
the next press switches back. The console prints which mode is now on. Names already on screen may only change after
their window refreshes, for example after selecting something else and back.

## Install

Put `Sam2017_MoreConsoleCommandsSED.asi` in the `plugins` folder next to `Sam2017_SeriousEditor.exe`, together with
the `msimg32.dll` loader next to the exe (same setup as the other Sam2017 plugins). In the game
(`Sam2017_Unrestricted.exe`) it does nothing. What it did is written to `Sam2017_SeriousEditor.asi.log` next to the exe.

## How it works

The plugin redirects five calls in memory. The exe file on disk is never changed.

- **Declaring the variables.** Cvars are declared by static record tables passed to the editor's
  `RegisterDeclarations` (0x140B2E3E0) while each engine module initializes. The plugin redirects that call in one
  "Core" module initializer (0x140B6A72B) and registers its own table right after, exactly like built-in cvars.
  Normal ones use flags 0x10002 (like `md_bLoadIncompatibleTypes`); saved ones add 0x4, giving 0x10006 (like
  `hud_bShowCoords`).
- **Help records.** The editor loads the help files once, through a loader called from three places: the list of
  all cvars (0x14075B0D6), the `help` command (0x14075B281) and the lookup used by the console tooltip (0x14075BFCD).
  After the loader has run, the plugin completes the records for its variables: it adds missing ones, puts
  `(undocumented)` where there is no description, and sets the type and saved fields.
- **The command.** The editor's startup fetches `CommandHandler.Global` (0x140160B48) right before registering its
  tool commands. The plugin adds its command and shortcut there.
- **Write access.** `res_bSaveInTextFormat` lives in the read-only `.rdata` section, so its page is made writable.
  The toggle makes its one byte of code writable only for the moment it changes it.
- **Checks.** Before changing anything, the plugin checks:
  - the exe build (TimeDateStamp, SizeOfImage, CheckSum);
  - FNV-1a hashes of all the code it relies on, including the code around each redirected call and the instructions
    that read each variable;
  - the original bytes of the five calls;
  - the name getter at 0x140B730F0. Either byte value is accepted there, so an exe already set to code names still
    works.

  If anything differs, it changes nothing and says why.

## Adding more variables

Add a line to `g_cvars[]` in `src/Sam2017_MoreConsoleCommandsSED.c`. Give the name, the address minus 0x140000000, the
type name, whether it is saved, and optional min/max values. Then run `src/build.sh`, which needs clang, lld-link and
llvm-dlltool (no Windows SDK or C runtime). If you add code the plugin relies on, add it to `src/gen_expect.py`, which
generates `exe_expect.h` from the original exe.

## Test harness

`test/harness.c` (Linux x86-64) maps the real exe at 0x140000000, loads the plugin with a fake kernel32 and runs the
editor's own code:

- the Core module initialization, cvar assignment, and the console's write handler (with min/max clamping);
- the editor's own saving to and loading from `SeriousEditor.ini`, with only the file stream faked;
- the help loader through all three call sites, with and without help-file records, and the tooltip builder;
- creation of `CommandHandler.Global` and the real `RegisterCommand`/`RegisterShortcut`;
- key presses dispatched by the editor's shortcut handling.

    gcc -O1 -o harness test/harness.c
    ./harness Sam2017_SeriousEditor.exe Sam2017_MoreConsoleCommandsSED.asi realcore
    # other modes: full realcmd cookie late twice conflict relied game wrongbuild pretoggled badgetter
