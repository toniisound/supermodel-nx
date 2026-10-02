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
 * SwitchGLDebug.h
 *
 * SWITCH_GL_CHECK("where"): on the Switch build, reads pending OpenGL errors
 * and logs them with the checkpoint name (the first few times per
 * checkpoint), so an error can be pinned to the code that raised it.
 * Compiles to nothing elsewhere.
 */

#ifndef INCLUDED_SWITCHGLDEBUG_H
#define INCLUDED_SWITCHGLDEBUG_H

#ifdef __SWITCH__
void SwitchGLCheck(const char *where);
#define SWITCH_GL_CHECK(where) SwitchGLCheck(where)
#else
#define SWITCH_GL_CHECK(where) ((void)0)
#endif

#endif // INCLUDED_SWITCHGLDEBUG_H
