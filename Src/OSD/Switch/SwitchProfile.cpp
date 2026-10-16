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
#include "CPU/PowerPC/ppc.h"
#include <GL/glew.h>

#include <algorithm>
#include <cstring>
#include <vector>
#include <string>

namespace SwitchProfile
{
  uint64_t tileGenNs = 0;
  uint64_t ppcExecNs = 0;
  uint64_t ppcExecCalls = 0;
  uint64_t ppcCycles = 0;
  uint64_t frames = 0;
  bool logCpuWait = false;
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

    // I/O reads: direct-mapped table like the block entries
    constexpr unsigned kIoSlots = 1024;
    uint32_t s_ioKey[kIoSlots];
    uint32_t s_ioCount[kIoSlots];
    uint8_t s_ioSize[kIoSlots];
    uint64_t s_ioLast[kIoSlots];
    uint64_t s_ioReads = 0;

    // Interrupts by bit (8 maskable + a few non-maskable)
    uint32_t s_irqCount[16];
    unsigned s_irqEnable = 0;

    // History ring: kind 0 = block entry, 1 + type = exception
    struct Trace { uint32_t pc, msr; int kind; };
    constexpr unsigned kTraceSize = 4096;
    Trace s_trace[kTraceSize];
    unsigned s_traceHead = 0;
    uint32_t s_excCount[32];

    struct DecWrite { uint32_t pc, value, old; bool armed; };
    constexpr unsigned kDecRing = 8;
    DecWrite s_decRing[kDecRing];
    unsigned s_decHead = 0;
    uint64_t s_decWrites = 0;

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

  void CountIoRead(uint32_t addr, unsigned size, uint64_t value)
  {
    s_ioReads++;
    unsigned slot = ((addr >> 2) ^ (addr >> 12)) & (kIoSlots - 1);
    if (s_ioKey[slot] != addr) {
      if (s_ioCount[slot] > 64) { s_ioCount[slot]--; return; }
      s_ioKey[slot] = addr;
      s_ioCount[slot] = 0;
    }
    s_ioCount[slot]++;
    s_ioSize[slot] = (uint8_t)size;
    s_ioLast[slot] = value;
  }

  void TraceEntry(uint32_t pc, uint32_t msr)
  {
    Trace &t = s_trace[s_traceHead++ & (kTraceSize - 1)];
    t.pc = pc; t.msr = msr; t.kind = 0;
  }

  void TraceSlice(uint32_t pc, uint32_t msr)
  {
    Trace &t = s_trace[s_traceHead++ & (kTraceSize - 1)];
    t.pc = pc; t.msr = msr; t.kind = 99;
  }

  void TraceException(int type, uint32_t srr0, uint32_t msr)
  {
    Trace &t = s_trace[s_traceHead++ & (kTraceSize - 1)];
    t.pc = srr0; t.msr = msr; t.kind = 1 + type;
    s_excCount[type & 31]++;
  }

  void CountDecWrite(uint32_t pc, uint32_t value, uint32_t old, bool armed)
  {
    s_decWrites++;
    s_decRing[s_decHead++ & (kDecRing - 1)] = { pc, value, old, armed };
  }

  void CountIrq(unsigned bits, unsigned enable)
  {
    for (unsigned i = 0; i < 16; i++)
      if (bits & (1u << i)) s_irqCount[i]++;
    s_irqEnable = enable;
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
    {
      static UINT64 s_lastCompiled = 0, s_lastFlushes = 0;
      UINT64 compiled = 0, flushes = 0;
      ppc_debug_jit_stats(&compiled, &flushes);
      InfoLog("JIT per frame: %.1f blocks compiled, %.2f cache flushes",
              (compiled - s_lastCompiled) / f, (flushes - s_lastFlushes) / f);
      s_lastCompiled = compiled;
      s_lastFlushes = flushes;
    }
    len = snprintf(line, sizeof(line), "JIT block entries per frame: %.0f; hottest:", s_entries / f);
    for (size_t i = 0; i < pcs.size() && i < 6; i++)
      len += snprintf(line + len, sizeof(line) - len, " %08X x%.0f", pcs[i].pc, pcs[i].count / f);
    InfoLog("%s", line);

    // When the CPU sits in one place (a game waiting for something), show the
    // code there, the registers and the RAM words its loads read, once per
    // new place: enough to see what it waits for.
    static uint32_t s_dumpedPc = 0;
    if (logCpuWait && !pcs.empty() && pcs[0].pc < 0x00800000 && pcs[0].pc != s_dumpedPc &&
        pcs[0].count / f >= 300.0 && s_entries < 4000ull * frames)
    {
      s_dumpedPc = pcs[0].pc;
      InfoLog("CPU waiting at %08X: MSR=%08X LR=%08X", pcs[0].pc, ppc_read_msr(), ppc_get_lr());
      for (int r = 0; r < 32; r += 8)
        InfoLog("  r%-2d %08X %08X %08X %08X %08X %08X %08X %08X", r,
                ppc_get_gpr(r), ppc_get_gpr(r + 1), ppc_get_gpr(r + 2), ppc_get_gpr(r + 3),
                ppc_get_gpr(r + 4), ppc_get_gpr(r + 5), ppc_get_gpr(r + 6), ppc_get_gpr(r + 7));
      {
        char exl[256];
        int el = snprintf(exl, sizeof(exl), "  exceptions taken per frame (by type):");
        for (int t = 0; t < 32; t++)
          if (s_excCount[t]) el += snprintf(exl + el, sizeof(exl) - el, " %d x%.1f", t, s_excCount[t] / f);
        InfoLog("%s", exl);
        // History, oldest first: SLICE = start of a ppc_execute() call, then
        // the JIT block entries (pc, msr) and exceptions (type at srr0).
        // Repeating runs of up to 6 entries are folded into one line "xN".
        const unsigned head = s_traceHead;
        const unsigned count = head < kTraceSize ? head : kTraceSize;
        std::vector<Trace> h(count);
        for (unsigned k = 0; k < count; k++)
          h[k] = s_trace[(head - count + k) & (kTraceSize - 1)];
        auto same = [](const Trace &a, const Trace &b) { return a.pc == b.pc && a.kind == b.kind && a.msr == b.msr; };
        std::vector<std::string> lines;
        for (size_t i = 0; i < h.size(); )
        {
          size_t bestP = 1, bestK = 1;
          for (size_t p = 1; p <= 6 && i + p <= h.size(); p++)
          {
            size_t k = 1;
            while (i + (k + 1) * p <= h.size())
            {
              bool eq = true;
              for (size_t j = 0; j < p && eq; j++)
                eq = same(h[i + j], h[i + k * p + j]);
              if (!eq) break;
              k++;
            }
            if (k > 1 && k * p > bestK * bestP) { bestP = p; bestK = k; }
          }
          std::string s;
          char b[96];
          for (size_t j = 0; j < bestP; j++)
          {
            const Trace &t = h[i + j];
            if (t.kind == 0)       snprintf(b, sizeof(b), "%s%08X", j ? " " : "", t.pc);
            else if (t.kind == 99) snprintf(b, sizeof(b), "%sSLICE@%08X", j ? " " : "", t.pc);
            else                   snprintf(b, sizeof(b), "%sEXC%d@%08X", j ? " " : "", t.kind - 1, t.pc);
            s += b;
          }
          snprintf(b, sizeof(b), "  msr=%08X", h[i].msr);
          s += b;
          if (bestK > 1) { snprintf(b, sizeof(b), "  x%zu", bestK); s += b; }
          lines.push_back(s);
          i += bestP * bestK;
        }
        InfoLog("  recent history (%u events, oldest first):", count);
        for (size_t i = lines.size() > 150 ? lines.size() - 150 : 0; i < lines.size(); i++)
          InfoLog("    %s", lines[i].c_str());
      }
      if (pcs.size() > 1 && pcs[1].pc < 0x00800000)
      {
        InfoLog("  code at %08X (next hottest):", pcs[1].pc);
        for (uint32_t a = pcs[1].pc - 0x10; a < pcs[1].pc + 0x30; a += 4)
        {
          const uint32_t op = ppc_read_opcode_at(a);
          char mnem[64] = "?", oprs[128] = "";
          DisassemblePowerPC(op, a, mnem, oprs, true);
          InfoLog("  %c %08X: %08X  %-8s %s", a == pcs[1].pc ? '>' : ' ', a, op, mnem, oprs);
        }
      }
      // Registers set by lis/li inside the window are tracked so the address
      // of "lis rX,hi; lwz rY,lo(rX)" is right (the live GPRs belong to
      // whatever code ran last, often an interrupt handler).
      std::vector<uint32_t> watched;
      {
        bool known[32] = {};
        uint32_t kval[32] = {};
        for (uint32_t a = pcs[0].pc - 0x20; a < pcs[0].pc + 0x40; a += 4)
        {
          const uint32_t op = ppc_read_opcode_at(a);
          char mnem[64] = "?", oprs[128] = "";
          DisassemblePowerPC(op, a, mnem, oprs, true);
          char extra[64] = "";
          const uint32_t primary = op >> 26, rD = (op >> 21) & 31, rA = (op >> 16) & 31;
          const uint32_t simm = (uint32_t)(int32_t)(int16_t)(op & 0xFFFF);
          if (primary >= 32 && primary <= 35 && rA != 0)   // lwz/lwzu/lbz/lbzu: show the word read
          {
            const uint32_t ea = (known[rA] ? kval[rA] : ppc_get_gpr(rA)) + simm;
            if (ea < 0x00800000)
            {
              snprintf(extra, sizeof(extra), "  ; [%08X] = %08X%s", ea, ppc_read_opcode_at(ea & ~3u), known[rA] ? "" : " (?)");
              if (known[rA] && watched.size() < 4 &&
                  std::find(watched.begin(), watched.end(), ea) == watched.end())
                watched.push_back(ea);
            }
          }
          InfoLog("  %c %08X: %08X  %-8s %s%s", a == pcs[0].pc ? '>' : ' ', a, op, mnem, oprs, extra);
          // Track constants
          if (primary == 15 && rA == 0)      { known[rD] = true; kval[rD] = (op & 0xFFFF) << 16; }
          else if (primary == 14 && rA == 0) { known[rD] = true; kval[rD] = simm; }
          else if (primary == 24 && rD == rA && known[rA]) { kval[rA] |= (op & 0xFFFF); }   // ori
          else if (primary >= 32 && primary <= 47 && primary != 36 && primary != 37 && primary != 38 &&
                   primary != 39 && primary != 44 && primary != 45 && primary != 47)
            known[rD] = false;               // loads overwrite rD
          else if (primary == 31 || primary == 14 || primary == 15 || primary == 12 || primary == 13 ||
                   primary == 7 || primary == 8 || primary == 21 || primary == 23)
            known[rD] = false, known[rA] = false;
        }
      }

      // Who writes the words the waiting code reads? Scan the code in RAM
      // for stores to each address made with "lis rX,hi ... st* rY,lo(rX)".
      std::vector<uint32_t> storePcs;
      for (uint32_t ea : watched)
      {
        InfoLog("  [%08X] = %08X now; stores to it found in RAM code:", ea, ppc_read_opcode_at(ea & ~3u));
        const uint32_t hi = (ea + 0x8000) >> 16, lo = ea & 0xFFFF;
        int hits = 0;
        for (uint32_t a = 0x100; a < 0x00400000 && hits < 6; a += 4)
        {
          const uint32_t op = ppc_read_opcode_at(a);
          const uint32_t primary = op >> 26;
          if (!(primary == 36 || primary == 37 || primary == 38 || primary == 44 ||
                primary == 32 || primary == 34) || (op & 0xFFFF) != lo)
            continue;
          const uint32_t rA = (op >> 16) & 31;
          bool match = false;
          for (uint32_t b = 4; b <= 0x40 && b <= a; b += 4)
          {
            const uint32_t pop = ppc_read_opcode_at(a - b);
            if ((pop >> 26) == 15 && ((pop >> 16) & 31) == 0 && ((pop >> 21) & 31) == rA)
            {
              match = ((pop & 0xFFFF) == hi);
              break;
            }
          }
          if (!match || primary == 32 || primary == 34)
            continue;
          hits++;
          if (a + 0x40 < pcs[0].pc || a > pcs[0].pc + 0x40)
            storePcs.push_back(a);
          InfoLog("    store at %08X:", a);
          for (uint32_t c = a - 0x30; c <= a + 0x0C; c += 4)
          {
            const uint32_t cop = ppc_read_opcode_at(c);
            char mnem[64] = "?", oprs[128] = "";
            DisassemblePowerPC(cop, c, mnem, oprs, true);
            InfoLog("    %c %08X: %08X  %-8s %s", c == a ? '>' : ' ', c, cop, mnem, oprs);
          }
        }
        if (!hits)
          InfoLog("    none (written through a pointer?)");
      }

      // How does the code reach those stores? Print the function holding
      // each one, then who calls it (bl or a pointer in RAM), two levels up.
      {
        auto funcStart = [](uint32_t a) -> uint32_t {
          for (uint32_t b = a; b + 0x1000 > a && b >= 0x100; b -= 4)
            if ((ppc_read_opcode_at(b) & 0xFFFF0000) == 0x94210000)   // stwu r1,-n(r1)
              return b;
          return a;
        };
        auto printCode = [](uint32_t from, uint32_t to, uint32_t mark) {
          for (uint32_t c = from; c <= to; c += 4)
          {
            const uint32_t op = ppc_read_opcode_at(c);
            char mnem[64] = "?", oprs[128] = "";
            DisassemblePowerPC(op, c, mnem, oprs, true);
            InfoLog("    %c %08X: %08X  %-8s %s", c == mark ? '>' : ' ', c, op, mnem, oprs);
          }
        };
        auto printFunc = [&](uint32_t start, uint32_t mark) {
          uint32_t end = start;
          for (int n = 0; n < 160; n++, end += 4)
            if (ppc_read_opcode_at(end) == 0x4E800020 && end >= mark) break;   // blr
          printCode(start, end, mark);
        };
        auto callers = [](uint32_t f, std::vector<uint32_t> &out) {
          for (uint32_t a = 0x100; a < 0x00400000 && out.size() < 4; a += 4)
          {
            const uint32_t op = ppc_read_opcode_at(a);
            if ((op >> 26) == 18 && (op & 3) == 1)                  // bl
            {
              int32_t d = (int32_t)(op & 0x03FFFFFC);
              if (d & 0x02000000) d -= 0x04000000;
              if (a + d == f) out.push_back(a);
            }
            else if (op == f)
              out.push_back(a | 1);                                 // a pointer to f
          }
        };
        std::vector<uint32_t> done;
        std::vector<std::pair<uint32_t, uint32_t>> todo;   // (code address, depth)
        for (uint32_t s : storePcs) todo.push_back({ s, 0 });
        for (size_t i = 0; i < todo.size() && i < 12; i++)
        {
          const uint32_t at = todo[i].first, depth = todo[i].second;
          const uint32_t f = funcStart(at);
          if (std::find(done.begin(), done.end(), f) != done.end()) continue;
          done.push_back(f);
          InfoLog("  function %08X (holds %08X, level %u):", f, at, depth);
          printFunc(f, at);
          std::vector<uint32_t> cs;
          callers(f, cs);
          if (cs.empty()) InfoLog("    called from: nowhere found (computed call?)");
          for (uint32_t c : cs)
          {
            if (c & 1) { InfoLog("    pointer to it at %08X", c & ~1u); continue; }
            InfoLog("    called from %08X", c);
            if (depth < 2) todo.push_back({ c, depth + 1 });
          }
        }
      }
    }

    // Most polled I/O registers, with the last value read
    struct Io { uint32_t count; uint32_t addr; unsigned size; uint64_t last; };
    std::vector<Io> ios;
    for (unsigned i = 0; i < kIoSlots; i++)
      if (s_ioCount[i]) ios.push_back({ s_ioCount[i], s_ioKey[i], s_ioSize[i], s_ioLast[i] });
    std::sort(ios.begin(), ios.end(), [](const Io &a, const Io &b) { return a.count > b.count; });
    len = snprintf(line, sizeof(line), "I/O reads per frame: %.0f; most read:", s_ioReads / f);
    for (size_t i = 0; i < ios.size() && i < 6; i++)
      len += snprintf(line + len, sizeof(line) - len, " %08X/%u x%.0f =%llX", ios[i].addr, ios[i].size,
                      ios[i].count / f, (unsigned long long)ios[i].last);
    InfoLog("%s", line);

    {
      len = snprintf(line, sizeof(line), "Exceptions per frame (by type, 1 IRQ 2 DEC):");
      bool any = false;
      for (int t = 0; t < 32; t++)
        if (s_excCount[t]) { len += snprintf(line + len, sizeof(line) - len, " %d x%.1f", t, s_excCount[t] / f); any = true; }
      if (!any) len += snprintf(line + len, sizeof(line) - len, " none");
      InfoLog("%s", line);
      UINT32 dec = 0; int pending = 0, trigger = 0, ratio = 0;
      ppc_debug_dec_state(&dec, &pending, &trigger, &ratio);
      len = snprintf(line, sizeof(line), "Decrementer: DEC=%08X pending=%X trigger=%d ratio=%d, writes per frame %.1f; last:",
                     dec, pending, trigger, ratio, s_decWrites / f);
      const unsigned n = s_decHead < kDecRing ? s_decHead : kDecRing;
      for (unsigned k = 0; k < n; k++)
      {
        const DecWrite &w = s_decRing[(s_decHead - n + k) & (kDecRing - 1)];
        len += snprintf(line + len, sizeof(line) - len, " %08X:%08X(was %08X%s)", w.pc, w.value, w.old, w.armed ? ",due" : "");
      }
      InfoLog("%s", line);
      s_decWrites = 0;
    }

    len = snprintf(line, sizeof(line), "IRQs raised (enable mask %02X):", s_irqEnable & 0xFF);
    bool anyIrq = false;
    for (unsigned i = 0; i < 16; i++)
      if (s_irqCount[i]) { len += snprintf(line + len, sizeof(line) - len, " %02X x%u", 1u << i, s_irqCount[i]); anyIrq = true; }
    if (!anyIrq)
      len += snprintf(line + len, sizeof(line) - len, " none");
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
    memset(s_ioKey, 0, sizeof(s_ioKey));
    memset(s_ioCount, 0, sizeof(s_ioCount));
    s_ioReads = 0;
    memset(s_irqCount, 0, sizeof(s_irqCount));
    memset(s_excCount, 0, sizeof(s_excCount));
  }
}
