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
 * SwitchJit.h
 *
 * Thin C wrapper over libnx's JIT API for the PowerPC recompiler.
 *
 * libnx's <switch.h> declares a global `Result` type that clashes with
 * Supermodel's own `Result`, so JitArm64.cpp talks to libnx only through
 * these functions.
 *
 * The code buffer is mapped twice: `rw` is written by the recompiler and
 * `rx` is executed. With the CodeMemory backend (the usual case under
 * Atmosphere) both views are always mapped. With the SetProcessMemoryPermission
 * fallback the RX view is unmapped between switch_jit_begin_write() and
 * switch_jit_end_write().
 */

#ifndef INCLUDED_SWITCHJIT_H
#define INCLUDED_SWITCHJIT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  void *rw;             // write view
  void *rx;             // execute view
  int   always_mapped;  // 1 with CodeMemory, 0 with SetProcessMemoryPermission
} SwitchJitInfo;

// Returns 0 on success. On failure, *rc receives the libnx result code.
int  switch_jit_create(size_t size, SwitchJitInfo *info, unsigned *rc);
void switch_jit_close(void);

// Bracket every write to the code buffer.
void switch_jit_begin_write(void);
void switch_jit_end_write(void);

// Make `len` bytes written at `rw` (executed at `rx`) visible to instruction
// fetch. Call inside the write bracket.
void switch_jit_sync(void *rw, void *rx, size_t len);

#ifdef __cplusplus
}
#endif

#endif // INCLUDED_SWITCHJIT_H
