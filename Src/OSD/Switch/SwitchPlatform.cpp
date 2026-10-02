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
 * SwitchPlatform.cpp
 *
 * Nintendo Switch (libnx) support for the SDL front end. Deliberately does
 * not include Supermodel headers (see SwitchPlatform.h).
 */

#include "SwitchPlatform.h"

#include <switch.h>
#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static bool s_nxlink = false;

static bool FileExists(const std::string &path)
{
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static void MakeDir(const std::string &path)
{
  mkdir(path.c_str(), 0777);   // fails harmlessly if it already exists
}

static bool CopyFile(const std::string &from, const std::string &to)
{
  FILE *in = fopen(from.c_str(), "rb");
  if (!in)
    return false;
  FILE *out = fopen(to.c_str(), "wb");
  if (!out)
  {
    fclose(in);
    return false;
  }
  char buf[64 * 1024];
  size_t n;
  bool ok = true;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
  {
    if (fwrite(buf, 1, n, out) != n)
    {
      ok = false;
      break;
    }
  }
  fclose(in);
  fclose(out);
  return ok;
}

void SwitchPlatformInit()
{
#ifdef SWITCH_NXLINK
  // Debug builds (make NXLINK=1): send stdout/stderr to `nxlink -s` on the PC.
  if (R_SUCCEEDED(socketInitializeDefault()))
    s_nxlink = nxlinkStdio() >= 0;
#endif

  const std::string root = SWITCH_SUPERMODEL_ROOT;
  MakeDir("sdmc:/switch");
  MakeDir(root);
  for (const char *sub : { "Config", "ROMs", "NVRAM", "Saves", "Log", "Screenshots", "Assets" })
    MakeDir(root + "/" + sub);

  // The front end uses relative paths (ROMs/<game>.zip in the GUI, the
  // GameXMLFile setting), so run from the root folder.
  chdir(root.c_str());

  // First launch: install the bundled configuration. Existing files are never
  // overwritten, so the player's own settings and input mappings survive
  // updates of the .nro.
  if (R_SUCCEEDED(romfsInit()))
  {
    for (const char *file : { "Supermodel.ini", "Games.xml", "Music.xml" })
    {
      std::string dest = root + "/Config/" + file;
      if (!FileExists(dest))
        CopyFile(std::string("romfs:/Config/") + file, dest);
    }
    for (const char *file : { "p1crosshair.bmp", "p2crosshair.bmp" })
    {
      std::string dest = root + "/Assets/" + file;
      if (!FileExists(dest))
        CopyFile(std::string("romfs:/Assets/") + file, dest);
    }
    romfsExit();
  }
  // No CPU boost here: libnx's boost mode drops the GPU clock to 76.8 MHz.
  // Use an overclocking sysmodule (e.g. sys-clk) for more CPU speed.
}

void SwitchPlatformShutdown()
{
#ifdef SWITCH_NXLINK
  if (s_nxlink)
    socketExit();
#endif
  (void)s_nxlink;
}

void SwitchAddGamepadMappings()
{
  // SDL's Switch joystick driver reports buttons in this order:
  //   0 A, 1 B, 2 X, 3 Y, 4 L-stick, 5 R-stick, 6 L, 7 R, 8 ZL, 9 ZR,
  //   10 Plus, 11 Minus, 12 D-left, 13 D-up, 14 D-right, 15 D-down
  // and axes 0/1 left stick, 2/3 right stick. SDL ships a mapping for this
  // device with Xbox-style positions (the bottom button, B, acts as "A").
  // Replace it with the Nintendo layout: the button printed "A" acts as "A"
  // (confirm in the GUI, JOY1_BUTTON1 in Supermodel.ini), and so on.
  static const char *k_layout =
    "a:b0,b:b1,x:b2,y:b3,"
    "leftstick:b4,rightstick:b5,leftshoulder:b6,rightshoulder:b7,"
    "lefttrigger:b8,righttrigger:b9,start:b10,back:b11,"
    "dpleft:b12,dpup:b13,dpright:b14,dpdown:b15,"
    "leftx:a0,lefty:a1,rightx:a2,righty:a3";

  for (int i = 0; i < SDL_NumJoysticks(); i++)
  {
    char guid[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof(guid));
    std::string mapping = std::string(guid) + ",Switch Controller," + k_layout;
    SDL_GameControllerAddMapping(mapping.c_str());   // replaces SDL's own entry
  }
}

void SwitchPinCurrentThread(int core)
{
  if (core < 0 || core > 2)
    return;
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
}

int SwitchCoreForThread(const char *name)
{
  // The main thread (core 0) runs the front end and OpenGL rendering.
  // The PowerPC main board gets a core of its own; sound and drive boards
  // share the last one.
  if (name && strcmp(name, "MainBoard") == 0)
    return 1;
  return 2;
}

bool SwitchRelaunchToMenu(const char *argv0)
{
    if (!envHasNextLoad())
        return false;

    // argv[0] is the .nro's own path when started from the Homebrew Menu.
    std::string path = (argv0 && std::strncmp(argv0, "sdmc:/", 6) == 0) ? argv0 : SWITCH_SUPERMODEL_ROOT "/supermodel.nro";
    if (!FileExists(path))
        return false;

    // The loader's argv string starts with the program path, quoted.
    std::string args = "\"" + path + "\"";
    return R_SUCCEEDED(envSetNextLoad(path.c_str(), args.c_str()));
}
