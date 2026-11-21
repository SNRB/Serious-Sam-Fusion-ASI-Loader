# Sam2017_NetricsaMessagesSED.asi

ASI plugin for Serious Editor of Serious Sam Fusion 2017 (`Sam2017_SeriousEditor.exe`, build of 7 June 2019).
It makes the NETRICSA message properties of the Serious Sam HD format editable again: **Model animation**,
**Model size**, **Game size**, **Rotation**, **Offset**, **Image** and **Text**, as the 32-bit Serious Sam HD editor
showed them. The .rsc format does not change.

## What you get

Open a NETRICSA message (.rsc). The property sheet now shows:

| Property        | What it holds                                                                |
|-----------------|------------------------------------------------------------------------------|
| Category        | as before                                                                    |
| Title           | as before (2017 format)                                                      |
| HUD message     | as before (2017 format)                                                      |
| Model           | as before                                                                    |
| Grid opacity    | as before (2017 format)                                                      |
| Model animation | name of the model's animation to play (Serious Sam HD format)               |
| Model size      | number                                                                       |
| Game size       | number                                                                       |
| Rotation        | number                                                                       |
| Offset          | X, Y, Z                                                                      |
| Image           | a texture, chosen like any other texture link                                |
| Text            | the message text file (.txt), chosen like any other text resource            |

The last seven come back with the names the Serious Sam HD editor used. They follow Grid opacity because that is
their order in the declaration (and in the files).

The files already hold all twelve properties: the 2017 editor reads and writes every one of them, also in messages
made in the 2017 format, and messages in the Serious Sam HD format still open properly. The plugin only shows the
seven that the 2017 editor hides. A message saved with the plugin opens the same way in the editor without it and
in the game.

In messages made in the 2017 format, these seven fields were never set, so the editor shows whatever value was saved:
usually an empty Model animation, Image and Text, and junk numbers such as -4.3e-08 or NaN in the sizes, rotation
and offset.

## Install

Put `Sam2017_NetricsaMessagesSED.asi` in the `plugins` folder next to `Sam2017_SeriousEditor.exe`, together with the
`msimg32.dll` loader next to the exe (same setup as the other Sam2017 plugins, and it works alongside them). It must
be loaded before the program starts, which that loader does. In the game (`Sam2017_Unrestricted.exe`) it does
nothing. What it did is written to `Sam2017_SeriousEditor.asi.log` next to the exe:

    [Sam2017_NetricsaMessagesSED.asi] applied to Sam2017_SeriousEditor.exe (9 fields of the CNetricsaMessageData declaration, 0x1420c5010..0x1420c5280): NETRICSA messages show and edit "Model animation", "Model size", "Game size", "Rotation", "Offset", "Image" and "Text" again

## Why the 2017 editor hides them

A NETRICSA message is a `CNetricsaMessageData` resource with twelve members. The 2017 build kept all of them,
including the seven of the Serious Sam HD format (`nmd_idModelAnimation`, `nmd_fModelSize`, `nmd_fGameSize`,
`nmd_fRotation`, `nmd_vOffset`, `nmd_ptxImage`, `nmd_ptrText`), and its NETRICSA screen still reads them. In its
metadata, though, those seven have no UI name, and the property sheet shows only members that have one. The image
(a `ResourceLink<CTexture>`) and the text (a `Ptr<CTextResource>`) also lost their edit flags.

In the 32-bit Serious Sam HD editor (`SamHD_TSE_SeriousEditor.exe`) the same members have the UI names above, with
the flags `0x40000a` on the image and `0xa` on the text.

## How it works

The engine's metadata is compiled into static declaration records, which it turns into runtime types when the
program starts. `0x140EDC078` registers the declaration stream at `0x1420C4A10` that holds `CNetricsaMessageData`,
and `0x140B2DAD0` reads its member records: the UI name at `+0x18`, which it copies into an engine string, and the
flags at `+0x40`.

The plugin runs before that and changes nine fields of the `CNetricsaMessageData` declaration (all on one page of
the read-only data, made writable for the change and read-only again):

- The UI names of the seven members, which point to the empty string, now point to the names above.
- The flags of the image and text members are set to `0xa`. Every texture link and text resource the 2017 editor
  shows has the bits `0x2` and `0x8` (most have exactly `0xa`), and so does the message's Model. The Serious Sam HD
  editor's extra bit `0x400000` no longer exists in this build.

Nothing else changes. The members keep their order, ids, names, types and offsets, so the files are read and
written exactly as before. The UI name is not stored in files. Of the member flags, the file reader and writer use
only `0x100`, `0x400`, `0x1000` and `0x2000`, plus `0x2` for containers of pointers (these two members are not
containers). Bits `0x1`, `0x2`, `0x4` and `0x8` appear almost only on members that have a UI name (for example
`0x8`: on 1157 members with one, on 1 without): they are edit attributes the meta compiler writes with the UI name.

**Checks.** Before changing anything, the plugin checks:

- the exe build (TimeDateStamp, SizeOfImage, CheckSum, base address);
- that it was loaded before the program started;
- that the nine fields hold their original values (if all nine hold its own values, another copy already applied
  it);
- FNV-1a hashes of 18 regions: the rest of the declaration, and the engine code that registers and reads it.

If anything differs, it changes nothing, says why in the log and shows a message box.

## Building

Run `src/build.sh`, which needs clang, lld-link and llvm-dlltool (no Windows SDK or C runtime).
`src/gen_expect.py <exe> src/exe_expect.h` generates the build checks, hashes and field list from the original exe.
It also asserts everything the plugin assumes about the declaration: names, types, offsets, original values and
the page.

## Tests

`harness/` (Linux x86-64) maps the real exe at `0x140000000`, loads the plugin with a fake kernel32 and calls its
`DllMain` the way the loader does. Then it runs the editor's own code: the C runtime's static initializers and the
registration of the NETRICSA declaration stream. It reads the resulting runtime type:

- with the plugin, all twelve members have their UI names, and Model, Image and Text have the flags `0xa`;
- ids, names and offsets are the same as without it;
- nothing else in the program changed (only the nine fields differ from the loaded image).

The `baseline` mode does the same without the plugin and shows the seven members without a UI name.

    harness/rebuild.sh
    harness/harness Sam2017_SeriousEditor.exe Sam2017_NetricsaMessagesSED.asi full
    # other modes: baseline late cookie twice conflict relied game wrongbuild
