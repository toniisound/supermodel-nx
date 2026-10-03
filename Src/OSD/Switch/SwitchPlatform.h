/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **/

/*
 * SwitchPlatform.h
 *
 * Nintendo Switch (libnx) support for the SDL front end.
 *
 * Everything that needs <switch.h> lives in SwitchPlatform.cpp, which does not
 * include Supermodel headers: libnx's global `Result` type clashes with
 * Supermodel's `enum class Result`.
 */

#ifndef INCLUDED_SWITCHPLATFORM_H
#define INCLUDED_SWITCHPLATFORM_H

// Root folder on the SD card. Everything Supermodel reads or writes lives here:
//   Config/  ROMs/  NVRAM/  Saves/  Log/  Screenshots/  Assets/
#define SWITCH_SUPERMODEL_ROOT "sdmc:/switch/supermodel"

// Call first thing in main(): creates the folders above, makes the root the
// current directory (Games.xml paths and the GUI's ROMs/ are relative) and
// copies the bundled Config files from RomFS on first launch.
void SwitchPlatformInit();
void SwitchPlatformShutdown();

// Once SDL's game controller subsystem is up: gives the Switch pads a game
// controller mapping (Nintendo layout: A on the right is "A"), so the setup
// GUI and InputSystem = "sdlgamepad" work with Joy-Cons and the Pro Controller.
void SwitchAddGamepadMappings();

// Moves the calling thread to `core` (0-2). The kernel never migrates a
// homebrew thread by itself, and new threads start on the creator's core.
void SwitchPinCurrentThread(int core);

// Picks the core for an emulator thread by its CThread name.
int SwitchCoreForThread(const char *name);

// Asks the homebrew loader to start this .nro again, with no arguments (so it
// opens the game list), as soon as the program exits. Used by the "exit game"
// combo (- and +) when the game was picked in the GUI. Returns false when there
// is no homebrew loader to do it (e.g. an .nro loaded directly by an emulator).
bool SwitchRelaunchToMenu(const char *argv0);

// True once each time − and + start being held together on player 1 / the
// handheld Joy-Cons. Read straight from the HID state, so it works whatever
// Supermodel.ini maps. Call once per frame.
bool SwitchExitComboPressed();

// Copies the Supermodel.ini bundled in the .nro (RomFS) to `dest`, for the
// GUI's "Load Defaults". Returns false if it could not.
bool SwitchCopyBundledConfig(const char *dest);

#endif // INCLUDED_SWITCHPLATFORM_H
