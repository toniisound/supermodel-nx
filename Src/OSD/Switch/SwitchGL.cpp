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
