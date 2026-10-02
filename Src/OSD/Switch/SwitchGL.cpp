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
 * SwitchGL.cpp
 *
 * glewInit() for the Switch build: loads glad's GL function pointers from the
 * current EGL context (see include/GL/glew.h).
 */

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include "SwitchGLDebug.h"
#include "OSD/Logger.h"

#include <cstring>

extern "C" {

unsigned char glewExperimental = 1;

unsigned int glewInit(void)
{
  int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
  if (version == 0)
    return GLEW_ERROR_NO_GL_VERSION;
  return GLEW_OK;
}

const char *glewGetErrorString(unsigned int error)
{
  if (error == GLEW_OK)
    return "no error";
  return "could not load OpenGL functions (no current context?)";
}

} // extern "C"

// See SwitchGLDebug.h. Each checkpoint logs its first 3 errors.
void SwitchGLCheck(const char *where)
{
  struct Seen { const char *where; unsigned count; };
  static Seen seen[64];
  static unsigned used = 0;

  for (int i = 0; i < 8; i++)
  {
    GLenum e = glGetError();
    if (e == GL_NO_ERROR)
      return;

    Seen *entry = nullptr;
    for (unsigned j = 0; j < used; j++)
      if (seen[j].where == where || strcmp(seen[j].where, where) == 0) { entry = &seen[j]; break; }
    if (!entry && used < 64)
    {
      seen[used] = { where, 0 };
      entry = &seen[used++];
    }
    if (entry && entry->count < 3)
    {
      entry->count++;
      ErrorLog("GL error 0x%04X before checkpoint '%s'%s", e, where,
               entry->count == 3 ? " (no more reports for this checkpoint)" : "");
    }
  }
}
