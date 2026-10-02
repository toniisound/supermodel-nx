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
 * SwitchJit.c
 *
 * libnx JIT wrapper used by Src/CPU/PowerPC/Jit/JitArm64.cpp. See SwitchJit.h.
 */

#include "SwitchJit.h"
#include <switch.h>

static Jit  s_jit;
static bool s_created = false;

// SetProcessMemoryPermission fallback only: the range written while the RX
// view was unmapped, whose instruction cache lines must be invalidated once
// the RX view is mapped again.
static u8 *s_dirty_lo = NULL;
static u8 *s_dirty_hi = NULL;

static bool always_mapped(void)
{
  return s_jit.type == JitType_CodeMemory;
}

int switch_jit_create(size_t size, SwitchJitInfo *info, unsigned *rc_out)
{
  Result rc = jitCreate(&s_jit, size);
  if (R_FAILED(rc))
  {
    if (rc_out)
      *rc_out = rc;
    return -1;
  }
  s_created = true;

  // The fallback backend starts out writable with no RX view; map it now so
  // both addresses are valid outside of a write bracket.
  if (!always_mapped())
    jitTransitionToExecutable(&s_jit);

  info->rw = jitGetRwAddr(&s_jit);
  info->rx = jitGetRxAddr(&s_jit);
  info->always_mapped = always_mapped() ? 1 : 0;
  return 0;
}

void switch_jit_close(void)
{
  if (!s_created)
    return;
  jitClose(&s_jit);
  s_created = false;
  s_dirty_lo = s_dirty_hi = NULL;
}

void switch_jit_begin_write(void)
{
  // CodeMemory keeps both views mapped and switch_jit_sync() keeps them
  // coherent block by block. Calling jitTransitionToExecutable() for it would
  // flush the entire buffer on every compile, so do nothing.
  if (s_created && !always_mapped())
    jitTransitionToWritable(&s_jit);
}

void switch_jit_end_write(void)
{
  if (!s_created || always_mapped())
    return;
  jitTransitionToExecutable(&s_jit);
  if (s_dirty_lo)
  {
    u8 *rx = (u8 *)jitGetRxAddr(&s_jit) + (s_dirty_lo - (u8 *)jitGetRwAddr(&s_jit));
    armICacheInvalidate(rx, (size_t)(s_dirty_hi - s_dirty_lo));
    s_dirty_lo = s_dirty_hi = NULL;
  }
}

void switch_jit_sync(void *rw, void *rx, size_t len)
{
  if (len == 0)
    return;

  // Clean the data cache through the view that was written.
  armDCacheFlush(rw, len);

  if (always_mapped())
  {
    // Invalidate the instruction cache through the view that is executed.
    armICacheInvalidate(rx, len);
  }
  else
  {
    // The RX view is unmapped while writing: remember the range for
    // switch_jit_end_write().
    u8 *lo = (u8 *)rw;
    u8 *hi = lo + len;
    if (!s_dirty_lo || lo < s_dirty_lo) s_dirty_lo = lo;
    if (!s_dirty_hi || hi > s_dirty_hi) s_dirty_hi = hi;
  }
}
