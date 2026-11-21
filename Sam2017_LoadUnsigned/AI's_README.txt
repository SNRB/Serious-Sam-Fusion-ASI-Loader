Sam2017_LoadUnsigned.asi  -  version 1.0
Load valid assets that are missing the editor signature
Serious Sam Fusion 2017, build of 7 June 2019:
    Sam2017_SeriousEditor.exe and Sam2017_Unrestricted.exe
==========================================================================

WHAT IT DOES
------------
By default the editor and the game refuse to load an asset that is not signed
with a sufficient (EDITOR) signature:

    '<file>' isn't signed with sufficient credentials (EDITOR)
    Stream is signed with insufficient credentials.

This plugin makes both programs load such assets anyway. The signature check
itself is left in place - the engine still reads the SIGSTRM header and still
verifies a signature that IS present (a present but broken signature still
fails, exactly as before). Only the final "this stream does not carry
sufficient credentials" decision is changed, so a missing or insufficient
signature no longer stops the asset from loading: the stream continues down
the same path a correctly signed one would.

So:
  - An unsigned but otherwise valid asset (e.g. Shield_nosig.mdl) loads.
  - A correctly signed asset (e.g. Shield_sig.mdl) loads as before.
  - A file that is damaged in some other way still fails for that reason.

INSTALL
-------
It uses the same msimg32.dll ASI loader as the other Sam2017 mods: msimg32.dll
next to the exe, plugins in the "plugins" folder next to the exe.

    Copy Sam2017_LoadUnsigned.asi into that plugins folder.

Both the editor and the game need it, so if they use different plugins
folders, put a copy in each. Unlike the other plugins it patches code only, so
it does not matter whether it loads before or after the engine starts.

At every start it writes Sam2017_LoadUnsigned.log next to itself. It should say
"applied: Sam2017_SeriousEditor.exe" (or Sam2017_Unrestricted.exe). If
something prevents it from working (a different exe build, or code already
changed by another mod), it shows one message box, writes the reason to the
log, and leaves the program untouched.

NOTES
-----
  - This only removes the credentials requirement. It does not sign anything
    and does not change any files on disk - it is the opposite approach to
    sesigner.exe (which adds a signature to each file). Use whichever fits:
    this plugin if you just want unsigned content to load on your machine;
    sesigner if you want the files themselves to carry a signature (for
    example to share them with people who do not run this plugin).
  - Anyone who loads a level or mod that depends on unsigned assets needs this
    plugin too, or those assets must be signed with sesigner instead.
  - It does not touch the executable's own self-signature check or any license
    check - only the asset/stream credentials check.
  - Works together with Sam2017_MultiPathMarker.asi, Sam2017_CellShaded.asi,
    Sam2017_MultiSpawnerOrder.asi and Sam2017_AutoPatrol.asi (none of them
    patch the same code).

HOW IT WORKS (all in memory, the exe files are not modified)
------------------------------------------------------------
Two places decide a stream has insufficient credentials and raise the errors
above; both keep running the whole check, only the "reject" branch is
redirected to the check's normal success path:

  - The stream-stack credential check: after the engine has looked for a
    sufficient signature, the test that turns "none found" into the error is
    replaced by an unconditional jump to the success continuation (the same
    place reached when a sufficient signature IS found).
  - A second gate (a virtual check on the current signature context that
    raises the same error): its conditional "proceed" branch is made
    unconditional.

Every patch site is matched against its expected bytes first; if the bytes
differ (another mod, or a different build) nothing is changed.

The source is in the "source" folder: src\ is the plugin (C, no runtime
library, built with clang + lld-link, see build.sh), tools\ finds and verifies
the patch sites in both executables (sig_sites.py writes src\targets.inc;
verify_patch.py applies the patches to a copy and disassembles the result),
test\ runs both checks (set SAM2017_BIN to the folder with the two exes).
