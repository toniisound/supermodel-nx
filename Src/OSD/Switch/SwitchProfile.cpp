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
 * SwitchProfile.cpp: see SwitchProfile.h.
 */

#include "SwitchProfile.h"
#include "Supermodel.h"
#include "OSD/Logger.h"
#include "CPU/PowerPC/PPCDisasm.h"
#include <GL/glew.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace SwitchProfile
{
  uint64_t tileGenNs = 0;
  uint64_t ppcExecNs = 0;
  uint64_t ppcExecCalls = 0;
  uint64_t ppcCycles = 0;
  uint64_t frames = 0;
  uint64_t renderTexNs = 0;
  uint64_t render2DNs = 0;
  uint64_t render3DNs = 0;
  uint64_t renderEndNs = 0;
  uint64_t renderAANs = 0;
  uint64_t swapNs = 0;
  uint64_t renderFrames = 0;
  uint64_t audioCallbacks = 0;
  uint64_t audioLateCallbacks = 0;
  uint64_t audioMaxGapNs = 0;
  uint64_t audioPeriodNs = 0;
  uint64_t audioUnderRuns = 0;
  uint64_t audioOverRuns = 0;

  namespace
  {
    // GPU timestamps. Each frame uses a free set of queries; a set stays
    // pending until the GPU has its results (checked without waiting).
    // Frames that find no free set are not measured.
    constexpr int kGpuSets = 8;
    enum { GpuSetFree, GpuSetMarking, GpuSetPending };
    GLuint s_gpuQueries[kGpuSets][GpuStageCount];
    int s_gpuState[kGpuSets];
    unsigned s_gpuMarked[kGpuSets];     // bit per stage marked in that frame
    bool s_gpuInit = false;
    int s_gpuSet = -1;                  // set being marked this frame, -1: none
    uint64_t s_gpuStageNs[GpuStageCount];
    uint64_t s_gpuFrames = 0;

    // Fallback counts: primary opcode (64) and the extended opcodes of the
    // four extended groups (19, 31, 59, 63), 1024 each.
    uint32_t s_primary[64];
    uint32_t s_extended[4][1024];
    uint32_t s_sample[64 + 4 * 1024];   // one opcode per slot, for the mnemonic
    uint64_t s_fallbacks = 0;

    // Block entry PCs: direct-mapped table, counts only.
    constexpr unsigned kPcSlots = 4096;
    uint32_t s_pcKey[kPcSlots];
    uint32_t s_pcCount[kPcSlots];
    uint64_t s_entries = 0;

    int GroupIndex(uint32_t primary)
    {
      switch (primary) { case 19: return 0; case 31: return 1; case 59: return 2; case 63: return 3; }
      return -1;
    }
  }

  void CountFallback(uint32_t opcode)
  {
    s_fallbacks++;
    uint32_t primary = opcode >> 26;
    int g = GroupIndex(primary);
    if (g < 0) {
      s_primary[primary]++;
      s_sample[primary] = opcode;
    } else {
      uint32_t ext = (opcode >> 1) & 0x3ff;
      s_extended[g][ext]++;
      s_sample[64 + g * 1024 + ext] = opcode;
    }
  }

  void CountBlockEntry(uint32_t pc)
  {
    s_entries++;
    unsigned slot = (pc >> 2) & (kPcSlots - 1);
    if (s_pcKey[slot] != pc) {        // keep the busier of two colliding PCs
      if (s_pcCount[slot] > 64) { s_pcCount[slot]--; return; }
      s_pcKey[slot] = pc;
      s_pcCount[slot] = 0;
    }
    s_pcCount[slot]++;
  }

  // Reads the results of pending sets the GPU has finished; never waits.
  static void GpuHarvest()
  {
    for (int set = 0; set < kGpuSets; set++) {
      if (s_gpuState[set] != GpuSetPending)
        continue;
      GLint available = 0;
      glGetQueryObjectiv(s_gpuQueries[set][GpuFrameEnd], GL_QUERY_RESULT_AVAILABLE, &available);
      if (!available)
        continue;
      GLuint64 t[GpuStageCount];
      for (int i = 0; i < GpuStageCount; i++)
        glGetQueryObjectui64v(s_gpuQueries[set][i], GL_QUERY_RESULT, &t[i]);
      for (int i = 1; i < GpuStageCount; i++)
        s_gpuStageNs[i] += t[i] - t[i - 1];
      s_gpuFrames++;
      s_gpuState[set] = GpuSetFree;
    }
  }

  void GpuMark(GpuStage stage)
  {
    if (stage == GpuFrameStart) {
      if (!s_gpuInit) {
        glGenQueries(kGpuSets * GpuStageCount, &s_gpuQueries[0][0]);
        for (int set = 0; set < kGpuSets; set++) s_gpuState[set] = GpuSetFree;
        s_gpuInit = true;
      }
      GpuHarvest();
      s_gpuSet = -1;
      for (int set = 0; set < kGpuSets && s_gpuSet < 0; set++) {
        if (s_gpuState[set] == GpuSetFree) s_gpuSet = set;
      }
      if (s_gpuSet < 0)
        return;                         // all sets still pending: skip this frame
      s_gpuState[s_gpuSet] = GpuSetMarking;
      s_gpuMarked[s_gpuSet] = 0;
    }
    if (s_gpuSet < 0)
      return;
    glQueryCounter(s_gpuQueries[s_gpuSet][stage], GL_TIMESTAMP);
    s_gpuMarked[s_gpuSet] |= 1u << stage;
  }

  void GpuEndFrame()
  {
    if (s_gpuSet < 0)
      return;
    // Only complete frames count (white frames skip the inner 3D marks)
    const unsigned all = (1u << GpuStageCount) - 1;
    s_gpuState[s_gpuSet] = (s_gpuMarked[s_gpuSet] == all) ? GpuSetPending : GpuSetFree;
    s_gpuSet = -1;
  }

  void Report()
  {
    if (frames == 0)
      return;
    const double f = double(frames);

    InfoLog("Main board per frame: tile generator %.2f ms, PowerPC %.2f ms (%.0f calls, %.2f M cycles)",
            tileGenNs / 1e6 / f, ppcExecNs / 1e6 / f, ppcExecCalls / f, ppcCycles / 1e6 / f);

    // Top interpreter fallbacks
    struct Item { uint32_t count; uint32_t opcode; };
    std::vector<Item> items;
    for (uint32_t p = 0; p < 64; p++)
      if (s_primary[p]) items.push_back({ s_primary[p], s_sample[p] });
    for (int g = 0; g < 4; g++)
      for (uint32_t e = 0; e < 1024; e++)
        if (s_extended[g][e]) items.push_back({ s_extended[g][e], s_sample[64 + g * 1024 + e] });
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.count > b.count; });

    char line[512];
    int len = snprintf(line, sizeof(line), "JIT interpreter fallbacks per frame: %.0f;", s_fallbacks / f);
    for (size_t i = 0; i < items.size() && i < 8; i++) {
      char mnem[64] = "?", oprs[128] = "";
      DisassemblePowerPC(items[i].opcode, 0, mnem, oprs, true);
      len += snprintf(line + len, sizeof(line) - len, " %s %.0f", mnem, items[i].count / f);
    }
    InfoLog("%s", line);

    // Hottest block entry points
    struct Pc { uint32_t count; uint32_t pc; };
    std::vector<Pc> pcs;
    for (unsigned i = 0; i < kPcSlots; i++)
      if (s_pcCount[i]) pcs.push_back({ s_pcCount[i], s_pcKey[i] });
    std::sort(pcs.begin(), pcs.end(), [](const Pc &a, const Pc &b) { return a.count > b.count; });
    len = snprintf(line, sizeof(line), "JIT block entries per frame: %.0f; hottest:", s_entries / f);
    for (size_t i = 0; i < pcs.size() && i < 6; i++)
      len += snprintf(line + len, sizeof(line) - len, " %08X x%.0f", pcs[i].pc, pcs[i].count / f);
    InfoLog("%s", line);

    if (renderFrames)
    {
      const double rf = double(renderFrames);
      InfoLog("Render per frame (ms): 3D texture uploads %.2f, 2D layers %.2f, 3D scene %.2f, end of frame %.2f, copy to screen %.2f, swap %.2f",
              renderTexNs / 1e6 / rf, render2DNs / 1e6 / rf, render3DNs / 1e6 / rf, renderEndNs / 1e6 / rf, renderAANs / 1e6 / rf, swapNs / 1e6 / rf);
    }
    if (s_gpuInit)
      GpuHarvest();
    if (s_gpuFrames)
    {
      const double gf = double(s_gpuFrames) * 1e6;
      InfoLog("GPU per frame (ms): uploads + 2D bottom %.2f, 3D setup %.2f, 3D layers %.2f, 3D composite %.2f, 2D top %.2f, final copy %.2f, total %.2f (%llu frames)",
              s_gpuStageNs[Gpu2DBottomDone] / gf, s_gpuStageNs[Gpu3DDrawStart] / gf, s_gpuStageNs[Gpu3DDrawEnd] / gf,
              s_gpuStageNs[Gpu3DDone] / gf, s_gpuStageNs[Gpu2DTopDone] / gf, s_gpuStageNs[GpuFrameEnd] / gf,
              (s_gpuStageNs[Gpu2DBottomDone] + s_gpuStageNs[Gpu3DDrawStart] + s_gpuStageNs[Gpu3DDrawEnd] + s_gpuStageNs[Gpu3DDone] +
               s_gpuStageNs[Gpu2DTopDone] + s_gpuStageNs[GpuFrameEnd]) / gf,
              (unsigned long long)s_gpuFrames);
    }
    std::memset(s_gpuStageNs, 0, sizeof(s_gpuStageNs));
    s_gpuFrames = 0;

    renderTexNs = render2DNs = render3DNs = renderEndNs = renderAANs = swapNs = renderFrames = 0;

    if (audioCallbacks)
    {
      InfoLog("Audio: %llu callbacks of %.1f ms, %llu late (max gap %.1f ms), buffer under-runs %llu, over-runs %llu",
              (unsigned long long)audioCallbacks, audioPeriodNs / 1e6, (unsigned long long)audioLateCallbacks,
              audioMaxGapNs / 1e6, (unsigned long long)audioUnderRuns, (unsigned long long)audioOverRuns);
    }
    audioCallbacks = audioLateCallbacks = audioMaxGapNs = audioUnderRuns = audioOverRuns = 0;

    // Reset
    tileGenNs = ppcExecNs = ppcExecCalls = ppcCycles = frames = 0;
    memset(s_primary, 0, sizeof(s_primary));
    memset(s_extended, 0, sizeof(s_extended));
    s_fallbacks = 0;
    memset(s_pcKey, 0, sizeof(s_pcKey));
    memset(s_pcCount, 0, sizeof(s_pcCount));
    s_entries = 0;
  }
}
