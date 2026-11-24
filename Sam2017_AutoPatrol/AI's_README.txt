Sam2017_AutoPatrol.asi  -  version 1.1
"Auto patrol" for all character entities
Serious Sam Fusion 2017, builds of 7 June 2019 and 19 November 2026:
    Sam2017_SeriousEditor.exe and Sam2017_Unrestricted.exe
==========================================================================

CHANGES
-------
1.1  Works with the updated executables of 19 November 2026 (and still with
     the ones of 7 June 2019); the plugin picks the right addresses for the
     exe it is loaded into. Everything it changes is the same code in the new
     build (byte for byte apart from addresses), only moved. The type
     description it extends is 0x60 qwords longer in the new build (other
     types in it changed), while the CPuppetProperties record in it is
     unchanged (still format 151, 0x188 bytes, padding at 0x184), so the new
     member is added in exactly the same way; the length of the description
     and the positions the plugin edits in it are now per-build values in
     src\targets.inc. Levels saved with 1.0 load with 1.1 and the other way
     round (the property is the same: ID 152, format 152), in both builds.

INSTALL
-------
It uses the same msimg32.dll ASI loader as Sam2017_MultiPathMarker.asi and
Sam2017_CellShaded.asi: msimg32.dll next to the exe, plugins in the "plugins"
folder next to the exe.

    Copy Sam2017_AutoPatrol.asi into that plugins folder.

Both the editor and the game need it, so if they use different plugins
folders, put a copy in each. At every start the plugin writes
Sam2017_AutoPatrol.log next to itself. It should say
"applied: Sam2017_SeriousEditor.exe" (or Sam2017_Unrestricted.exe). If
something prevents it from working (a different exe build, an ASI loader that
loads plugins too late, code already changed by another mod), it shows one
message box, writes the reason to the log, and leaves the program untouched.

USE
---
Every character entity (Legged character, Aircraft, Turret, Hovercraft,
Rolling ball, Spaceship, SS1 characters, ... - everything whose properties
derive from the puppet properties) now has an "Auto patrol" check box. It is
the last of the character properties, right after "Active range override"
(before "Spawn flags"). It is off by default.

When it is checked and the character has a Tactic - its own "Tactic", or the
"Tactics holder" of the spawner that spawned it - then every time the
character starts that tactic, it takes the path marker nearest to where it is
standing as its Tactic marker. That replaces any marker that was set on the
character ("Tactic marker"), on the spawner ("Tactic marker") or on the
tactic itself ("Tactic marker"). After that the tactic works exactly as
before: "Ordered moving using markers", "Move on random marker", "Moving on
curve", ... all behave as usual, only the first target marker is the nearest
one.

  - The tactic starts right after the character is spawned (or when the level
    starts, for characters placed in the level), so that is when the nearest
    marker is taken. If the same tactic is started again later (for example
    with the "Reuse tactic" behavior action), the nearest marker is looked up
    again, from where the character is at that moment.
  - Any other tactic (a "Next tactic", or a tactic given by a script) works as
    without the plugin: its own marker first, then the character's Tactic
    marker.
  - Without a Tactic, the check box does nothing.

Which markers count:
  - Path marker, Enemy marker (SS1), Cover marker, and marker types added by
    plugins, e.g. the MultiPath marker of Sam2017_MultiPathMarker.asi.
  - Not: markers on the character's own Intro path and Outro path (a spawned
    character that has walked its intro path is standing on it).
  - Not: Path / Enemy / Cover markers that are not part of a path (no next and
    no previous marker), e.g. markers used only as positions by scripts.
  - Not: the SS1 boss markers (Pyramid spaceship, Summoner, UghZan).
  - "Nearest" is the straight-line distance, the same measure the engine uses
    for its own "closest marker" search. If no marker qualifies, the normal
    rules apply.

In the editor you see the effect when you simulate the level; nothing changes
while editing.

COMPATIBILITY - PLEASE READ
---------------------------
  - Levels saved before installing the plugin load and play exactly as
    before (all characters start with "Auto patrol" off).
  - A level saved WITH the plugin installed contains the new property: the
    character properties' format goes from 151 to 152. Such a level needs the
    plugin to load, in the editor and in the game, even if no character uses
    "Auto patrol" - and that is every level that contains a character. Without
    the plugin the engine refuses it ("the file was saved by a newer version of
    the application"). This is the same as with levels that use
    MultiPathMarker or the Multi spawner order plugin, but here it applies to
    nearly every level, so ship the plugin with your levels.
  - Savegames: characters that were already in the level when the game was
    saved do not remember the check box after loading. If they start their
    tactic again, they go to the marker they had picked before (it is their
    Tactic marker), unless the tactic has a marker of its own. Characters
    spawned after loading work normally.
  - Works together with Sam2017_MultiPathMarker.asi, Sam2017_CellShaded.asi
    and Sam2017_MultiSpawnerOrder.asi (none of them patch the same code).

HOW IT WORKS (all in memory, the exe files are not modified)
------------------------------------------------------------
  - The type description that contains CPuppetProperties (the base of all
    character properties) is copied and registered instead of the original,
    with a new member BOOL ep_bAutoPatrol ("Auto patrol", member ID 152) at
    offset 0x184. That offset is 4 bytes of padding at the end of the
    0x188-byte object (and of all 34 derived classes), so no object size and
    no existing offset changes.
  - The CPuppetProperties constructor, used by all derived classes, also
    clears offset 0x184.
  - CPuppetEntity::InitializeFromProperties: every character, placed or
    spawned, is initialized from its properties there. The plugin notes which
    character entities have "Auto patrol" checked (the properties of spawned
    characters are deleted right after they are created).
  - Where the character mover manager starts a tactic, the lookup of the
    tactic's own marker is redirected. For an "Auto patrol" character starting
    its own tactic, the nearest marker is written to the character's tactic
    marker and the engine is told that the tactic has no marker, so it
    continues with the character's (new) tactic marker.

The source is in the "source" folder: src\ is the plugin (C, no runtime
library, built with clang + lld-link, see build.sh), test\ is a native test of
the logic (with mock engine objects) and of the rebuilt type description,
tools\ are the scripts used to find and verify every address in the four
executables (ap_sites.py writes src\targets.inc; it reads the exes of 7 June
2019 from SAM2017_BIN and the ones of 19 November 2026 from SAM2017_BIN_2026,
and checks that every site in the new build is the same code as in the old
one). test\run_tests.sh runs the logic test and, with both variables set,
rebuilds the type description for all four exes and checks it.
