# Serious Sam Fusion 2017 ASI Loader
A DLL hook for Serious Sam 2017's Unrestricted game and Serious Editor allowing loading ASI plugins into exe, plus a few scrips.

Tested in Singleplayer on custom levels, compatible with save games, does not alter entity formats aside from 2 plugins mentioned at the very bottom of the list.

❗❗❗NOT YET TESTED IN MULTIPLAYER❗❗❗

# Compatibility
works on 440323 public Steam version (current ATM), plugins simply refuse to load on non-matching versions, loader and plugins only affect unrestricted exe and the editor, I will not provide support in altering non-moddable version of the game.

# Installing / Uninstalling
Download Fusion-ASI-Loader-v1.rar and any plugins you want/need and drop the contents into main Serious Sam Fusion folder, loader is a single .dll library that hooks upon launching the game/exe and plugins are singlular script files loaded from bin/x64/plugins folder

To uninstall plugins temove them from the folder, and to uninstall loader completely simply remove the .dll library

# Game plugins VS Editor plugins:
Some plugins are designed to modify only the Editor, some are designed to patch both game and the editor with the same content

❗❗❗GAME PLUGINS REQUIRE Game-ExeSigBypass_v1 PLUGIN TO WORK❗❗❗

without it, your main game exe will crash on launch

# Current Editor plugins:

## NETRICSA Messages Restore
Restores editing NETRICSA messages the way it was possible in Serious Sam HD, functions are identical, SS3 messages will read garbage, unused data but can be safely edited and saved.

<img width="290" alt="2026-11-18 20_39_42-Serious Editor 2017 -  64-bit  -  SE2017_PC_dev  -  SeriousSam2017  -  Beta  - C" src="https://github.com/user-attachments/assets/da7623a5-b133-4b0b-9da4-345d637fe339" />

<img width="700" height="395" alt="2026-11-18 20_44_10-Simulation" src="https://github.com/user-attachments/assets/4480d74b-ebbf-4396-af1c-5fad7a6d59a0" />

## More Console Commands
Adds variety of console commands linked to cvars that are locked/read-only by default:

### Toggle metadata codenames (Ctrl+Shift+Alt+C)
Key combination restored from Serious Sam 2 - displays internal metadata of all assets, allows editing past stuff normally not possible, such as assets without edit data.

<img width="444" alt="2026-11-18 22_02_59-Serious Editor 2017 -  64-bit  -  SE2017_PC_dev  -  SeriousSam2017  -  Beta  - C" src="https://github.com/user-attachments/assets/d637e61b-f997-43df-b9f5-3e746c7a3278" />

### res_bSaveInTextFormat (0,1,2)
Save assets in plain text format instead of binary, 1 skips certain formats, 2 saves every file, function stays on upon restarting editor and is saved to SeriousEditor.ini

❗IMPORTANT: world files saved with this option have broken signature, they require SeSigning or Load Unsigned plugin below❗
### scr_bUnsecureScripting

<img width="599" alt="2026-11-18 20_56_53-Simulation" src="https://github.com/user-attachments/assets/816aa103-d720-4cde-b42c-4d0bd3ac66a6" />

### prj_bUnlockAutoShotgunScope

Re-enable scope for auto shotgun (requires world reload)

<img width="663" alt="2026-11-18 20_58_02-Simulation" src="https://github.com/user-attachments/assets/00f9e395-e768-4bce-818e-a30b86cf8404" />

### prj_bAllowLevelChangeInEditor

Completing level in editor will not quit the simulation and instead load the next level. Remains on upon restart and is saved to SeriousEditor.ini

### men_bMenuFrame

Re-eneables SS3's original frame around menu

### fxd_bShowDebug

Effect information debug display

<img width="533" alt="2026-11-18 21_00_45-Simulation" src="https://github.com/user-attachments/assets/fae804a0-2467-404d-9a7b-8357f65b590b" />

### fil_bReportFileOpen

<img width="365" alt="2026-11-18 21_01_57-Simulation" src="https://github.com/user-attachments/assets/717f86df-ef6e-4911-ab80-553ff28afc40" />

### md_bReportReadStats

Remains on upon restart and is saved to SeriousEditor.ini

<img width="373" alt="2026-11-18 21_03_07-Simulation" src="https://github.com/user-attachments/assets/8428e35f-3928-472a-9e19-285a19e5e662" />

### hud_bShowGeometryInfo

<img width="392" alt="2026-11-18 20_50_29-Simulation" src="https://github.com/user-attachments/assets/183ea930-6428-483a-99d1-76a013a36b1a" />


## Editor QOL Tools
To be expanded - a collection of minor patches to SED's various editors.

Currently has only 1 modification:
### Skeleton Editor
- Setting parent for multiple selected bones now properly parents them all to that new bone, instead of just the first selected one.

## Anim Button Rebind
A tiny thing, rebinds playback button from spacebar to backspace because it's been getting on my nerves.

# Current Game plugins:

## ExeSigBypass
Required for ALL Game plugins to load in the actual game's exe!

Bypasses game crash that unrestricted exe triggers upon detecting modification to itself, left for god knows what reasons as this verification check has been removed in the very next internal release and was never present in the editor to begin with.

## Cellshading
Adds new shader of "Cellshading" into the shader list when creating material, reusing code of Standard shader.

It mimmics classic sharp edge shading commonly used to give cartoony or anime-ish appearence in games.

Has options to control how shaded part should be recolored, threshold of shadow edge's sharpness, the intensity of shaded part and how much of the model should be shaded based on light direction.

Possibly can be expanded in the future to use color ramp and masks.

Models with cellshading can be safely loaded in plugin-less editor, the shader will simply get replaced with empty shader.

https://github.com/user-attachments/assets/77121eac-f190-4663-b97c-12bc294ba392

https://github.com/user-attachments/assets/0126a841-073d-49d5-8a8b-9ac3cde729b9

## SS4 MultipathMarker
Backport of Serious Sam 4's MultipathMarker entity - a variant of PathMarker that allows selecting multiple next path markers that puppet will randomly pick upon reaching the marker.

Defined as sub-class of original PathMarker entity, meaning it can be directly conneted with the PathMarker and vice-versa, as well as targetted by Tactics, compatible with Auto Patrol plugin.

Worlds with MultipathMarkers can be safely loaded in plugin-less game and editor, the marker simply gets removed.

<img width="780" alt="2026-11-18 22_06_20-Serious Editor 2017 -  64-bit  -  SE2017_PC_dev  -  SeriousSam2017  -  Beta  - C" src="https://github.com/user-attachments/assets/84ad94de-2776-49bb-85b9-a380ab07fb65" />

## Load Unsigned
Allows loading files with incorrect/missing signature, basically skipping usage of SeSigner, also allows loading world files saved with res_bSaveInTextFormat and may allow using SED's "finalize" option that removes Edit Data from files.

## SS2 Auto Patrol
❗❗❗IMPORTANT: Changes entity format. Worlds saved with this plugin installed require plugin, or md_bLoadIncompatibleTypes = 1 to load❗❗❗

Adds an "Auto Patrol" checkbox to all puppets, when checked upon spawning in the world, the puppet will immediatelly set it's nearest PathMarker (or MultipathMarker) as it's target marker and follow it using it's set Tactic.

This is essentially re-implementation of functionality that Serious Sam 2 had that was dropped in HD and up.

<img width="347" alt="2026-11-18 20_29_24-Serious Editor 2017 -  64-bit  -  SE2017_PC_dev  -  SeriousSam2017  -  Beta  - C" src="https://github.com/user-attachments/assets/3fd590f4-97d7-4727-ae4a-7d969729c78f" />


## Multi Spawner Expansion
❗❗❗IMPORTANT: Changes entity format. Worlds saved with this plugin installed require plugin, or md_bLoadIncompatibleTypes = 1 to load❗❗❗

Adds dropdown menu to MultiSpawnerEntity, that when used overwrites spawner's default behavior and adds new spawning methods.
If menu is on "Default", MultiSpawner functionality is unchanged, this is setting on any non-plugin worlds, leaving them unchanged.

Options:
### Groups
Spawns enemies in randomly ordered groups, with count of a group defined by enemy's weight, each position has the same chance to be selected, uses the same static seed as normal MultiSpawner, so order of spawning remains unchanged upon every loading of the level.
### Full Random
like randomize template order check, except makes MultiSpawner generate random seed every time the world is loaded, including loaded from Saves and upon subsequent re-spawns of all entities from it.
### Groups Full Random
mix of two above, spawns enemies in groups ordered by random seed each time the world is loaded.

<img width="379" alt="2026-11-18 20_05_57-Serious Editor 2017 -  64-bit  -  SE2017_PC_dev  -  SeriousSam2017  -  Beta  - C" src="https://github.com/user-attachments/assets/8eda8338-1849-4cd8-a3ee-8a95d268e61a" />

# Optional Test Levels:

I've attached two .gro files for anyone to test the functionality of these plugins, in-game they are in "SeriousNorbo's Test Maps" campaign:

SN_TestLevelforASIStuff.gro contains level with Multipath Marker, a Groups Full Random Multi Spawner, puppets that Auto Patrol upon spawn and a cellshaded Demon observing them.

SN_TestLevelforASIStuff_NOCUSTOM.gro is the same level WITHOUT the two scripts that use custom entities - Auto Patrol and custom Multi Spawner, it should even load in plugin-less game.

# Build from source
ASI_Loader, AnimButtonTweak, CellShaded, MultiPathMarker and SkipExeSignatureCheck may have differences in the way source code is written compared to further plugins, that is due to them being initially embedded modifications into EXE before being re-written as independend ASI Plugins.

# Special thanks to:
Nigmas for providing informations on Fusion's memory cvars to allow More Console Commands plugin to exist
Andeiki and Shockwave for testing the plugins on their games.

# AI
Yes, AI disassembled the exe and wrote plugins, because of course it did, stuff was firmly tested over weeks of use on different machines to confirm stability of this vibecode. Original readme files written by AI for each plugin has been left in the source code for help in research and reverse engineering of the code.
