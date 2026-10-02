/*
 * GL/glew.h for the Nintendo Switch build.
 *
 * There is no GLEW for the Switch, and Mesa's libglapi does not export the GL
 * entry points as symbols: every GL function, GL 1.x included, has to be
 * fetched with eglGetProcAddress. Supermodel's GL calls therefore go through
 * glad (Src/OSD/Switch/glad, generated for GL 4.5 compatibility), and
 * glewInit() loads it from the current context with SDL_GL_GetProcAddress.
 *
 * Call glewInit() after every SDL_GL_CreateContext()/SDL_GL_MakeCurrent().
 */
#ifndef SUPERMODEL_SWITCH_GLEW_SHIM_H
#define SUPERMODEL_SWITCH_GLEW_SHIM_H

#include <glad/gl.h>

#ifndef GLAPIENTRY
#  define GLAPIENTRY APIENTRY
#endif

#define GLEW_OK             0u
#define GLEW_NO_ERROR       0u
#define GLEW_ERROR_NO_GL_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif
extern unsigned char glewExperimental;   /* accepted and ignored */
unsigned int glewInit(void);
const char *glewGetErrorString(unsigned int error);
#ifdef __cplusplus
}
#endif

#endif /* SUPERMODEL_SWITCH_GLEW_SHIM_H */
