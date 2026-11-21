/*
 * Test harness for Sam2017_NetricsaMessagesSED.asi (Linux x86-64).
 *
 * Maps the real Sam2017_SeriousEditor.exe image at its fixed base 0x140000000, loads the plugin
 * DLL with a fake kernel32 and calls its DllMain the way the msimg32.dll loader does (before the
 * program starts).  Then it runs the editor's own code: the C runtime's static initializers, and
 * the registration of the declaration stream that holds CNetricsaMessageData (0x140EDC078 ->
 * 0x140B2E3E0), which builds the runtime type the property sheet shows.  The test reads the
 * runtime members of that type: their UI names, flags, ids and offsets.
 * Modes: full, baseline (the same without the plugin), late, cookie, twice, conflict, relied,
 * game, wrongbuild.
 */
#define _GNU_SOURCE
#include <math.h>
