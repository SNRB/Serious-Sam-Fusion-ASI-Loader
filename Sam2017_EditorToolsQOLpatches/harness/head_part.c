/*
 * Test harness for Sam2017_EditorToolsQOLpatches.asi (Linux x86-64).
 *
 * Maps the real Sam2017_SeriousEditor.exe image at its fixed base 0x140000000, loads the plugin
 * DLL with a fake kernel32, calls its DllMain, then runs the editor's own code:
 *   - its static initializers, object IDs and the document's undo journal;
 *   - a skeleton (CEditSkeleton with a LOD) and its bones made with the skeleton editor's own
 *     undoable functions (create bone, set name, parent and placement) in one undo step;
 *   - the skeleton editor's own bone selection object (vtable 0x14261A2E8) and its select and
 *     deselect functions, at +0x3A0 of a stand-in skeleton editor;
 *   - the tools Tool.SetBoneParent and Tool.SetBoneParentInPlace, made by their own factories and
 *     run through vtable slot 30, as the editor's tool activation runs them: with one bone selected
 *     the editor's Reparent (0x1419EE000), with several the plugin's;
 *   - the editor's undo and redo.
 * The skeleton editor's window and its text dialog (0x141673DB0) are faked: the dialog records
 * what it was asked and answers with the test's text.
 */
#define _GNU_SOURCE
#include <math.h>
