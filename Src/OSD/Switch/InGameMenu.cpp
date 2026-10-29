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
 * InGameMenu.cpp
 *
 * Pause menu of the Switch build (see InGameMenu.h).
 */

#include "InGameMenu.h"

#include <switch.h>
#include <GL/glew.h>
#include <SDL.h>
#include <string>
#include "Pkgs/imgui/imgui.h"
#include "Pkgs/imgui/imgui_impl_opengl3.h"

namespace InGameMenu
{
  namespace
  {
    struct Item { const char *label; Action action; };
    const Item kItems[] = {
      { "Save State", Action::SaveState },
      { "Load State", Action::LoadState },
      { "Exit",       Action::Exit },
      { "Cancel",     Action::Cancel },
    };
    constexpr int kItemCount = int(sizeof(kItems) / sizeof(kItems[0]));

    ImGuiContext *s_ctx = nullptr;
    bool s_open = false;
    bool s_hasSaveState = false;
    int s_slot = 0;
    int s_selected = 0;
    Action s_pending = Action::None;   // chosen, waiting for the buttons to be released

    PadState s_pad;
    bool s_padReady = false;

    std::string s_message;
    uint64_t s_messageEnd = 0;          // SDL performance counter

    constexpr u64 kMenuButtons =
      HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y |
      HidNpadButton_Plus | HidNpadButton_Minus | HidNpadButton_L | HidNpadButton_R |
      HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Up | HidNpadButton_Down |
      HidNpadButton_Left | HidNpadButton_Right;

    bool EnsureContext()
    {
      if (s_ctx)
        return true;
      ImGuiContext *previous = ImGui::GetCurrentContext();
      s_ctx = ImGui::CreateContext();
      ImGui::SetCurrentContext(s_ctx);
      ImGuiIO &io = ImGui::GetIO();
      io.IniFilename = nullptr;
      io.Fonts->AddFontDefaultVector();
      ImGui::StyleColorsDark();
      ImGuiStyle &style = ImGui::GetStyle();
      style.FontScaleMain = 1.6f;
      style.ScaleAllSizes(1.6f);
      style.WindowRounding = 8.0f;
      style.FrameRounding = 6.0f;
      const bool ok = ImGui_ImplOpenGL3_Init("#version 410");
      if (!ok)
      {
        ImGui::DestroyContext(s_ctx);
        s_ctx = nullptr;
      }
      ImGui::SetCurrentContext(previous);
      return ok;
    }

    bool ItemEnabled(int i)
    {
      return kItems[i].action != Action::LoadState || s_hasSaveState;
    }
  }

  void Open(bool hasSaveState, int slot)
  {
    s_slot = slot;
    if (!s_padReady)
    {
      padInitializeDefault(&s_pad);
      s_padReady = true;
    }
    padUpdate(&s_pad);          // - and + are held now: not "pressed" on the next update
    s_open = true;
    s_hasSaveState = hasSaveState;
    s_selected = 0;
    s_pending = Action::None;
  }

  bool IsOpen()
  {
    return s_open;
  }

  Action Update()
  {
    if (!s_open)
      return Action::None;

    padUpdate(&s_pad);
    const u64 down = padGetButtonsDown(&s_pad);
    const u64 held = padGetButtons(&s_pad);

    if (s_pending != Action::None)
    {
      if (held & kMenuButtons)
        return Action::None;
      const Action a = s_pending;
      s_pending = Action::None;
      s_open = false;
      return a;
    }

    if (down & (HidNpadButton_Up | HidNpadButton_StickLUp))
      s_selected = (s_selected + kItemCount - 1) % kItemCount;
    if (down & (HidNpadButton_Down | HidNpadButton_StickLDown))
      s_selected = (s_selected + 1) % kItemCount;

    if (down & HidNpadButton_A)
    {
      if (ItemEnabled(s_selected))
        s_pending = kItems[s_selected].action;
      else
        ShowMessage("No saved state for this game yet");
    }
    else if (down & HidNpadButton_B)
      s_pending = Action::Cancel;
    return Action::None;
  }

  void ShowMessage(const char *text)
  {
    s_message = text;
    s_messageEnd = SDL_GetPerformanceCounter() + 2 * SDL_GetPerformanceFrequency();
  }

  void Draw(int width, int height)
  {
    const bool showMessage = !s_message.empty() && SDL_GetPerformanceCounter() < s_messageEnd;
    if (!showMessage)
      s_message.clear();
    if (!s_open && !showMessage)
      return;
    if (!EnsureContext())
      return;

    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(width), float(height));
    io.DeltaTime = 1.0f / 60.0f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing;

    if (s_open)
    {
      ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 140));

      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowBgAlpha(0.92f);
      ImGui::Begin("##ingamemenu", nullptr, flags);
      ImGui::Text("Paused  -  save slot %d", s_slot);
      ImGui::Separator();
      const float itemWidth = ImGui::CalcTextSize("Load State (empty)").x + ImGui::GetStyle().FramePadding.x * 8;
      for (int i = 0; i < kItemCount; i++)
      {
        const bool enabled = ItemEnabled(i);
        if (!enabled)
          ImGui::BeginDisabled();
        const char *label = (!enabled && kItems[i].action == Action::LoadState) ? "Load State (empty)" : kItems[i].label;
        ImGui::Selectable(label, i == s_selected, 0, ImVec2(itemWidth, 0));
        if (!enabled)
          ImGui::EndDisabled();
      }
      ImGui::Separator();
      ImGui::TextDisabled("A: select   B: back");
      ImGui::End();
    }

    if (showMessage)
    {
      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 40.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
      ImGui::SetNextWindowBgAlpha(0.85f);
      ImGui::Begin("##ingamemessage", nullptr, flags);
      ImGui::TextUnformatted(s_message.c_str());
      ImGui::End();
    }

    ImGui::Render();

    GLint drawFbo = 0, viewport[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);

    ImGui::SetCurrentContext(previous);
  }

  void Shutdown()
  {
    s_open = false;
    s_pending = Action::None;
    s_message.clear();
    if (!s_ctx)
      return;
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGuiContext *ctx = s_ctx;
    s_ctx = nullptr;
    ImGui::SetCurrentContext(ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(previous == ctx ? nullptr : previous);
  }
}
