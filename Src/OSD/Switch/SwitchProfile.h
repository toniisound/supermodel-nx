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
 * SwitchProfile.h
 *
 * Lightweight profiling of the main board thread for the Switch build: how
 * the "PowerPC" frame time splits between the 2D tile generator and the
 * CPU, how often the JIT falls back to the interpreter (and for which
 * instructions), and which guest addresses the CPU keeps re-entering.
 * SwitchProfileReport() writes it to the log; Main.cpp calls it every ~5 s.
 */

#ifndef INCLUDED_SWITCHPROFILE_H
#define INCLUDED_SWITCHPROFILE_H

#ifdef __SWITCH__

#include <cstdint>
#include <chrono>

namespace SwitchProfile
{
  inline uint64_t NowNs()
  {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  // Main board thread (written there, read for the report: diagnostics only).
  extern uint64_t tileGenNs;      // TileGen.DrawLine() time
  extern uint64_t ppcExecNs;      // ppc_execute() time
  extern uint64_t ppcExecCalls;
  extern uint64_t ppcCycles;      // guest cycles requested
  extern uint64_t frames;

  // Render thread (CModel3::RenderFrame and the buffer swap)
  extern uint64_t renderTexNs;    // queued 3D texture uploads (Real3D BeginFrame)
  extern uint64_t render2DNs;     // tile generator layers
  extern uint64_t render3DNs;     // 3D scene (New3D RenderFrame)
  extern uint64_t renderEndNs;    // GPU/TileGen EndFrame
  extern uint64_t renderAANs;     // final copy to the screen (SuperAA)
  extern uint64_t swapNs;         // SDL_GL_SwapWindow
  extern uint64_t renderFrames;

  // SDL audio thread (Audio.cpp PlayCallback) and sound board thread (OutputAudio)
  extern uint64_t audioCallbacks;
  extern uint64_t audioLateCallbacks;   // gap since the previous one > 1.5x its period: the device ran dry
  extern uint64_t audioMaxGapNs;
  extern uint64_t audioPeriodNs;        // play time of one callback
  extern uint64_t audioUnderRuns;       // emulator ring buffer under-runs (stale audio replayed)
  extern uint64_t audioOverRuns;        // emulator ring buffer over-runs (a frame of audio dropped)

  // GPU time per stage of the rendered frame, from GL timestamp queries read
  // a few frames later (never waits for the GPU). Marks are issued in order
  // by CModel3::RenderFrame and CNew3D::RenderFrame on the render thread.
  enum GpuStage
  {
    GpuFrameStart,      // before texture uploads and the bottom 2D layers
    Gpu2DBottomDone,
    Gpu3DDrawStart,     // after the 3D vertex upload
    Gpu3DDrawEnd,       // after all 3D layers (opaque and transparent passes)
    Gpu3DDone,          // after the 3D composite
    Gpu2DTopDone,
    GpuFrameEnd,        // after the final copy to the screen
    GpuStageCount
  };
  void GpuMark(GpuStage stage);
  void GpuEndFrame();

  // Interpreter fallbacks from JIT code (ppc_dispatch_opcode)
  void CountFallback(uint32_t opcode);

  // C dispatch loop entries (one per JIT block or chain run)
  void CountBlockEntry(uint32_t pc);

  void Report();
}

#endif // __SWITCH__
#endif // INCLUDED_SWITCHPROFILE_H
