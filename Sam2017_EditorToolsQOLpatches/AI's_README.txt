# Sam2017_EditorToolsQOLpatches.asi

ASI plugin for Serious Editor of Serious Sam Fusion 2017 (`Sam2017_SeriousEditor.exe`, builds of 7 June 2019 and
19 November 2026) with quality-of-life fixes for the editor's tools. For now it has one:

**Skeleton editor: Reparent with several bones selected.** **Reparent** (Shift+S) and **Reparent in place**
(Shift+Alt+S) give the new parent to every selected bone, not only to the first one. You get one dialog and one undo
step. No file format changes.

## Changes for the editor of 19 November 2026

The plugin now works with the editor of 19 November 2026 as well as the one of 7 June 2019; it recognizes the build
and uses its addresses. The editor's Reparent, the two tools and every editor function the plugin calls are the same
code in the new build, only at other addresses, so it behaves exactly the same. In the new build the two slots are
`0x1426883F0` and `0x142688510` (vtables `0x142688300` and `0x142688420`), the editor's Reparent is `0x141A2ED70`.
`src/gen_expect.py` now reads both exes and writes a table per build into `src/exe_expect.h`; the test harness runs
on both builds (see Tests).

## What you get

Select some bones in the skeleton editor and press Shift+S (the editor lists the command as **Set bone parent...**;
Shift+Alt+S is **Set bone parent in place...**). The dialog names them:

    Reparent bones
    Choose new parent for the 3 selected bones: 'ArmL', 'ArmR' and 'Head'

It starts with the first bone's current parent, like the editor's own dialog. Type or pick the new parent and press
OK. Every selected bone then gets that parent, the same way the editor reparents one bone: it keeps its place in the
model (its placement relative to the new parent is set so that it does not move), and so do the bones under it. The
console says what was done:

    Reparent bones: 'ArmL', 'ArmR' and 'Head' are now under 'Spine' (kept in place).

One Ctrl+Z undoes it for all of them.

- **One bone selected** (or none): the editor's own Reparent runs, unchanged.
- **The new parent is one of the selected bones:** it keeps its own parent, the others go under it.
- **A selected bone that the new parent hangs below** keeps its parent, because the change would make a loop. The
  console says which bones were left as they were, and why:

      Reparent bones: 'Prop' is now under 'Head' (kept in place). Not changed: 'Spine' (the new parent hangs below it, so that would make a loop).

  If that leaves nothing to change, nothing is done and the console shows a warning.
- **An empty name** makes the bones roots (no parent), as the editor does for one bone.
- **A name no bone has** is set as their parent name, as the editor does for one bone; they then have no parent bone.
  The console points this out, so a typo is easy to notice (and to undo).
- **Several LODs:** each bone gets the bone of that name in its own LOD, as the editor looks up parents.
- **Cancel** changes nothing.

In this build, **Reparent** and **Reparent in place** run the same code, which keeps the bone in place (the "in place"
argument the editor gives it is not used). The plugin keeps that behaviour for both, for any number of bones.

## Install

Put `Sam2017_EditorToolsQOLpatches.asi` in the `plugins` folder next to `Sam2017_SeriousEditor.exe`, together with the
`msimg32.dll` loader next to the exe (the same setup as the other Sam2017 plugins, and it works alongside them). Unlike
them, it may also be loaded after the program has started, because the editor reads what it changes only when the
command runs. In the game (`Sam2017_Unrestricted.exe`) it does nothing. What it did is written to
`Sam2017_SeriousEditor.asi.log` next to the exe:

    [Sam2017_EditorToolsQOLpatches.asi] applied to Sam2017_SeriousEditor.exe (vtable slots 0x142622060 and 0x142622180): Skeleton editor "Reparent" (Shift+S) and "Reparent in place" (Shift+Alt+S) give the new parent to every selected bone

(with the editor of 19 November 2026 the slots are 0x1426883f0 and 0x142688510).

## How it works

Both commands are skeleton editor tools, `Tool.SetBoneParent` and `Tool.SetBoneParentInPlace`. When the editor runs
a tool it calls slot 30 of the tool's vtable (`0x142621F70` and `0x142622090`; the addresses in this section are
those of the 2019 build). For these two tools that slot leads to
the same function, `0x1419EE000`. It:

- takes the first bone of the selection list (`0x1419D9DB0`, in the order of selecting);
- asks for the parent name (`0x141673DB0`, with the hint list `HintList.Bones`);
- in one undo step:
  - sets the bone's parent name (`0x1419D2780`);
  - gives the bone the placement relative to its new parent that keeps it where it is (`0x1419D27A0`), from the
    bone's placement in the model before and the parent's after (`0x1419F5630`);
- rebuilds the skeleton tree.

The plugin changes the two slots (8 bytes each, in the program's read-only data, made writable for the change and
read-only again) to its own function:

- With one bone selected or none, it calls `0x1419EE000` as before.
- With several, it shows one dialog. It then decides which bones change from the skeleton as it is: not the new
  parent itself, and not a bone the new parent hangs below.
- In one undo step ("Reparent bones") it does, for each bone, what the editor does for one: it uses the same editor
  functions, sets the same relative placement (inverse of the new parent's placement in the model, times the bone's)
  and rebuilds the skeleton tree.

The skeleton changes through the editor's own undoable functions, so the skeleton files are written as the editor
always writes them.

**Checks.** Before changing anything, the plugin checks:

- the exe build (TimeDateStamp, SizeOfImage, CheckSum, base address): one of the two supported builds;
- that the two slots hold their original values;
- FNV-1a hashes of 39 regions: the editor's Reparent and the tools' registration (ids, factories, shortcuts), the
  rest of both vtables, and every editor function it calls.

If anything differs, it changes nothing, says why in the log and shows a message box. A second copy of the plugin in
the same process sees that the first one was applied and does nothing.

## Building

Run `src/build.sh`, which needs clang, lld-link and llvm-dlltool (no Windows SDK or C runtime).
`src/gen_expect.py <exe of 7 June 2019> <exe of 19 November 2026> src/exe_expect.h` generates the build checks,
hashes and slot lists from the two original exes. For each build it also asserts what the plugin assumes about the
two tools: their vtables and slot 30, their factories, ids and shortcuts, and the calls in the editor's Reparent.

## Tests

`harness/` (Linux x86-64) maps the real exe at `0x140000000`, loads the plugin with a fake kernel32 and calls its
`DllMain` the way the loader does. Then it runs the editor's own code:

- a skeleton (a `CEditSkeleton` with one LOD of 13 bones), made with the skeleton editor's own undoable functions
  (create bone, set name, parent and placement);
- the skeleton editor's own bone selection object and its select and deselect functions (the selection list keeps
  the order of selecting);
- the two tools made by their own factories and run through slot 30, with the editor's undo and redo.

Only the skeleton editor's window and the text dialog are faked; the dialog records what it is asked and gives the
test's answer. The tests check:

- with one bone, the editor's own Reparent runs (its dialog, the bone kept in place, undo);
- with three bones, one dialog that names them, and all three under the new parent;
- no bone moves in the model, children included;
- the first bone gets exactly the placement the editor's own Reparent gives it (to 6e-8);
- the console message, the skeleton tree rebuilt, one undo step that restores everything exactly, and redo;
- the new parent among the selection, a bone above the new parent (no loop is made), all of them above it (nothing
  done, a warning), an empty name, a name no bone has, Cancel, nothing selected (the editor's own message), and
  Reparent in place.

The `baseline` mode runs the same command without the plugin and shows that only the first bone gets the new parent.

    harness/rebuild.sh
    harness/harness Sam2017_SeriousEditor.exe Sam2017_EditorToolsQOLpatches.asi full      # editor of 7 June 2019
    harness/harness26 Sam2017_SeriousEditor.exe Sam2017_EditorToolsQOLpatches.asi full    # editor of 19 November 2026
    # other modes: baseline late twice conflict relied game wrongbuild

`harness26` is the same harness for the 2026 editor: `rebuild.sh` makes it from the same source with the editor
addresses replaced by those of `harness/addr_2026.txt` (`harness/apply_map.py`). All modes pass on both builds
(`full`: 38 checks), so the 2026 editor's own skeleton code was run with the plugin.
