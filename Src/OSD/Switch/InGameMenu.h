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
 * InGameMenu.h
 *
 * Pause menu of the Switch build, opened with - and + during a game:
 * Save State, Load State, Exit, Cancel. It reads the pad itself (libnx) and
 * draws with its own Dear ImGui context over the last emulated frame.
 */

#ifndef INCLUDED_INGAMEMENU_H
#define INCLUDED_INGAMEMENU_H

#ifdef __SWITCH__

namespace InGameMenu
{
  enum class Action { None, SaveState, LoadState, Exit, Cancel };

  // hasSaveState: a state file exists for this game in the current slot
  // (Load State is enabled); slot: the save state slot shown in the title
  void Open(bool hasSaveState, int slot);
  bool IsOpen();

  // Call once per frame while open. Returns the chosen action once the
  // buttons used to choose it are released (so the game does not see them),
  // and closes the menu; None while the player is still choosing.
  Action Update();

  // Short message shown at the bottom of the screen for a couple of seconds
  void ShowMessage(const char *text);

  // Draws the menu and/or the message over the current frame (end of frame,
  // before the buffer swap). width, height: size of the window in pixels.
  void Draw(int width, int height);

  // Frees the ImGui context and its GL objects (end of the game)
  void Shutdown();
}

#endif // __SWITCH__
#endif // INCLUDED_INGAMEMENU_H
