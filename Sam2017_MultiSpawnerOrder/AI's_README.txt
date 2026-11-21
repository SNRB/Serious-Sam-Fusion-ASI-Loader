Sam2017_MultiSpawnerOrder.asi  -  version 1.1
Template order methods for the Multi spawner
Serious Sam Fusion 2017, build of 7 June 2019:
    Sam2017_SeriousEditor.exe and Sam2017_Unrestricted.exe
==========================================================================

CHANGES
-------
1.1  Fixed: with more than 512 usable templates in the table, the three new
     methods only ever used the first 512 rows; the rows below them never
     spawned, not even when the spawner started over. Now every row takes
     part, in tables of any length. Tables of up to 512 usable rows give
     exactly the same spawn order as in 1.0, so levels that already worked
     do not change.

INSTALL
-------
It uses the same msimg32.dll ASI loader as Sam2017_MultiPathMarker.asi and
Sam2017_CellShaded.asi: msimg32.dll next to the exe, plugins in the "plugins"
folder next to the exe.

    Copy Sam2017_MultiSpawnerOrder.asi into that plugins folder
    (next to Sam2017_MultiPathMarker.asi).

Both the editor and the game need it, so if they use different plugins
folders, put a copy in each. At every start the plugin writes Sam2017_MultiSpawnerOrder.log
next to itself. It should say "applied: Sam2017_SeriousEditor.exe" (or
Sam2017_Unrestricted.exe). If something prevents it from working (a different
exe build, an ASI loader that loads plugins too late, code already changed by
another mod), it shows one message box, writes the reason to the log, and
leaves the program untouched.

USE
---
The Multi spawner's properties now have "Template order method", right under
"Randomize template order":

  Default             Exactly the old behaviour. The "Randomize template
                      order" checkbox decides, as before:
                        unchecked: table order, weight factor = how many;
                        checked:   shuffled with the spawner's static seed,
                                   weight factor = how often.
                      All multi spawners in existing levels are "Default".

  Groups              Each template spawns as one block of <Spawn weight
                      factor> monsters. The order of the blocks is shuffled
                      with the same static seed (the multi spawner's random
                      base), so the order is the same on every run, in the
                      editor and in the game.
                      Kleer 3, Gnaar 2, Biomech 1  ->  e.g.
                      Gnaar Gnaar Biomech Kleer Kleer Kleer

  Full random         Like "Randomize template order", but with a new seed
                      every time the level runs (every editor simulation,
                      every game start), so the order is different each run.

  Groups full random  Groups, with a new seed every run.

With the three new methods the "Randomize template order" checkbox is ignored.

Shared rules (the same as the original multi spawner):
  - One cycle = the sum of the weights, or the spawner's "Total number" if
    that is smaller. Spawn number i uses position (i mod cycle).
  - When "Total number" is at least the sum of the weights, every cycle
    contains each template exactly <weight> times (if it is smaller, only the
    first "Total number" positions are used, as in the original). With the
    two "full random" methods every cycle is shuffled anew; with Groups every
    cycle repeats the same order (as "Randomize template order" does).
  - Templates are filtered as before: "Valid difficulty", the "Multi spawner
    template" flag (with the same warning in the log). A weight of 0 is
    skipped.
  - There is no limit on the number of rows. Above 512 usable rows the
    groups are put in order by a random permutation of the whole table, which
    needs no extra memory; it costs well under a millisecond per spawn even
    for tables of thousands of rows.

COMPATIBILITY - PLEASE READ
---------------------------
  - Levels saved before installing the plugin load and play exactly as
    before.
  - A level saved WITH the plugin installed contains the new property: the
    Multi spawner's format goes from 5 to 6. Such a level needs the plugin to
    load, in the editor and in the game, even if all its multi spawners use
    "Default". Without the plugin the engine refuses it ("the file was saved
    by a newer version of the application"). This is the same as with levels
    that use MultiPathMarker.
  - Savegames: after loading a savegame, "Full random" and "Groups full
    random" pick a new seed for the remaining spawns.
  - Works together with Sam2017_MultiPathMarker.asi and Sam2017_CellShaded.asi
    (none of them patch the same code).

HOW IT WORKS (all in memory, the exe files are not modified)
------------------------------------------------------------
  - The Multi spawner's type description (meta data stream of
    CMultiSpawnerProperties) is copied and registered instead of the
    original, with a new enum ETemplateOrderMethod and a new member
    ep_etomTemplateOrderMethod (member ID 6) at offset 0x94. That offset is
    4 bytes of padding at the end of the 0x98-byte object, so the object size
    and every existing offset stay the same.
  - The three places that construct CMultiSpawnerProperties also clear offset
    0x94 (new spawners and spawners from old levels start as "Default").
  - CSpawnerEntity's constructor also clears its unused offset 0x14c, which
    holds the per-run seed of the "full random" methods.
  - The virtual GetTemplate(slot, total, difficulty) of
    CMultiSpawnerProperties (vtable + 0x130) is replaced. For "Default" it
    calls the original function.

The source is in the "source" folder: src\ is the plugin (C, no runtime
library, built with clang + lld-link, see build.sh), test\ is a native test of
the selection logic and of the rebuilt meta data stream, tools\ are the
scripts used to find and verify every address in both executables.
