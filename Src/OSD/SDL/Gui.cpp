#include "SDLIncludes.h"
#include <GL/glew.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <thread>
#include <map>
#include <set>
#include <system_error>
#include "GameLoader.h"
#include "../../Pkgs/imgui/imgui.h"
#include "../../Pkgs/imgui/imgui_internal.h"
#include "../../Pkgs/imgui/imgui_impl_sdl2.h"
#include "../../Pkgs/imgui/imgui_impl_opengl3.h"
#include "../../Util/NewConfig.h"
#include "../../Util/ConfigBuilders.h"
#include "../Src/OSD/SDL/SDLInputSystem.h"
#include "../Src/Inputs/Inputs.h"
#include "Main.h"
#ifdef __SWITCH__
#include "../Switch/SwitchPlatform.h"

static SDL_Window *s_guiWindow = nullptr;    // see TakeGuiWindow() in Gui.h
static SDL_GLContext s_guiContext = nullptr;
#endif

#ifdef _WIN32
    #include "../Src/OSD/Windows/DirectInputSystem.h"
#endif // _WIN32

/*
Quick description on the GUI stuff
----------------------------------

- Using imgui because I didn't want to suck in 1000 other dependancies.
- GUI will only show up if supermodel is run without command line paramaters, so existing loaders etc should be completely
  uneffected by this code.
- All controls are completely dynamic, they are created from the settings themselves. So if new settings are added/removed
  they will automatically show up in the GUI, no modifications are required to this code.
- To make this work I added some constraints to the options. For example now you can add a min/max for settings, or add a
  list of possible values. Previously without constraints the code might just silently fail or fall over if you passed
  invalid values.
- The constraints are variants. A c++ variant is exactly the same as a C union, just type safe. So for our options we can
  store bool, unsigned, int, float and std::string. More types could be added but this is enough for now.

  TODO
  ----

 - Some options are just enums which are raw numbers and not user friendly. Probably best replaced with strings.
 - Resolution stuff could be handled better, ie push a list of possible resolutions. Might need to combine into
   a single string or something.
 - Per game options. Need some code to get a diff of possible configs I think.
*/


static void WriteGameNode(Util::Config::Node& baseNode, const Util::Config::Node& diffNode, Util::Config::Node& writeNode, const std::string& group)
{
    for (const auto& n : baseNode) {

        if (n.IsLeaf() && n.Exists()) {

            auto& key = n.Key();
            auto  val = n.GetValue();

            if (val) {

                auto vRange = val->GetValueRange();

                if (vRange->GetGroup() != group) {
                    continue;
                }

                auto s1 = baseNode[key].ValueAs<std::string>();
                auto s2 = diffNode[key].ValueAs<std::string>();

                if (s1 != s2) {
                    writeNode[key] = s2;
                }
            }
        }
    }
}

static std::string NodeToString(Util::Config::Node& config)
{
    std::string s;

    for (const auto& n : config) {

        if (n.IsLeaf() && n.Exists()) {

            auto& key = n.Key();

            auto val = config[key].ValueAs<std::string>();

            s += key;
            s += " ";
            s += val;
            s += "\n";
        }
    }

    return s;
}

static void UpdateTempValues(Util::Config::Node& config, const std::string group, bool init)
{
    for (auto it = config.begin(); it != config.end(); ++it)
    {
        if (it->IsLeaf() && it->Exists()) {

            auto key = it->Key();
            auto val = it->GetValue();

            if (val) {

                auto vRange = val->GetValueRange();

                if (vRange) {

                    if (vRange->GetGroup() == group) {

                        auto index = vRange->GetIndex();

                        switch (index) {
                        case 0:             // bool
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<bool>(); }
                            else { config.Set(key, std::get<bool>(vRange->tempValue)); }
                            break;
                        }
                        case 1:             // unsigned
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<unsigned>(); }
                            else { config.Set(key, std::get<unsigned>(vRange->tempValue)); }
                            break;
                        }
                        case 2:             // int
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<int>(); }
                            else { config.Set(key, std::get<int>(vRange->tempValue)); }
                            break;
                        }
                        case 3:             // float
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<float>(); }
                            else { config.Set(key, std::get<float>(vRange->tempValue)); }
                            break;
                        }
                        case 4:             // std::string
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<std::string>(); }
                            else { config.Set(key, std::get<std::string>(vRange->tempValue)); }
                            break;
                        }
                        }

                        it->GetValue()->SetValueRange(vRange);      // setting a new key could erase value range so make sure to re-add it
                    }
                }
            }  
        }
    }
}

static void CreateControls(Util::Config::Node& config, const std::string group)
{
    for (auto it = config.begin(); it != config.end(); ++it)
    {
        if (it->IsLeaf() && it->Exists()) {

            auto key = it->Key();
            auto val = it->GetValue();

            if (val) {

                auto vRange = val->GetValueRange();

                if (vRange) {

                    if (vRange->GetGroup() == group) {

                        auto index = vRange->GetIndex();
                        auto& list = vRange->GetList();

                        // create a lambda to process combo box

                        auto ProcessCombo = [&](auto* valuePtr) 
                        {
                            using T = std::decay_t<decltype(*valuePtr)>;

                            int selectedIndex = 0;
                            int loopCount = 0;
                            std::vector<std::string> sVector;
                            std::vector<const char*> sVectorChar;

                            for (auto& l : list) {
                                auto value = std::get<T>(l);
                                sVector.emplace_back(std::to_string(value));

                                if (value == *valuePtr) {
                                    selectedIndex = loopCount;
                                }
                                loopCount++;
                            }

                            for (auto& s : sVector) {
                                sVectorChar.emplace_back(s.c_str());   // store pointer to the data
                            }

                            ImGui::Combo(key.c_str(), &selectedIndex, sVectorChar.data(), (int)sVectorChar.size());
                            vRange->tempValue = list[selectedIndex];
                        };

                        auto ProcessScalar = [&](auto valuePtr, ImGuiDataType type)
                        {
                            using T = std::decay_t<decltype(*valuePtr)>;
                            auto min_ = std::get<T>(vRange->GetMin());
                            auto max_ = std::get<T>(vRange->GetMax());
                            ImGui::SliderScalar(key.c_str(), type, valuePtr, &min_, &max_);
                        };
                        
                        auto ProcessControls = [&](auto valuePtr, ImGuiDataType type)
                        {
                            if (vRange->HasMinMax()) {
                                ProcessScalar(valuePtr, type);
                            }
                            else if (list.size()) {
                                ProcessCombo(valuePtr);
                            }
                            else {
                                ImGui::InputScalar(key.c_str(), type, valuePtr);
                            }
                        };


                        switch (index) {
                        case 0:             // bool
                        {
                            auto p = std::get_if<bool>(&vRange->tempValue);
                            ImGui::Checkbox(key.c_str(), p);
                            break;
                        }
                        case 1:             // unsigned
                        {
                            auto p = std::get_if<unsigned>(&vRange->tempValue);
                            ProcessControls(p, ImGuiDataType_U32);
                            break;
                        }
                        case 2:             // int
                        {
                            auto p = std::get_if<int>(&vRange->tempValue);
                            ProcessControls(p, ImGuiDataType_S32);
                            break;
                        }
                        case 3:             // float
                        {
                            auto p = std::get_if<float>(&vRange->tempValue);
                            ProcessControls(p, ImGuiDataType_Float);
                            break;
                        }
                        case 4:             // std::string
                        {
                            auto p = std::get_if<std::string>(&vRange->tempValue)->c_str();
                            auto& option = std::get<std::string>(vRange->tempValue);

                            auto& list = vRange->GetList();

                            if (list.size()) {

                                int selectedIndex = 0;
                                int loopCount = 0;
                                std::vector<const char*> sVector;

                                for (auto& l : list) {
                                    auto& item = std::get<std::string>(l);
                                    if (option == item) {
                                        selectedIndex = loopCount;
                                    }
                                    sVector.emplace_back(item.c_str());
                                    loopCount++;
                                }

                                ImGui::Combo(key.c_str(), &selectedIndex, sVector.data(), (int)sVector.size());
                                vRange->tempValue = list[selectedIndex];
                            }
                            else {
                                char buffer[256];
                                std::strcpy(buffer, p);
                                ImGui::InputText(key.c_str(), buffer, IM_ARRAYSIZE(buffer));
                                option = buffer;     // update temp buffer
                            }

                            break;
                        }
                        }
                    }
                }
            }
        }
    }
}

static void SetDefaultKeyVal(std::shared_ptr<CInput> input)
{
    std::string key = std::string("Input") + input->id;

    auto defaultConfig = DefaultConfig();

    auto mapping = defaultConfig[key.c_str()].ValueAs<std::string>();

    // update input with value from our default config
    input->SetMapping(mapping.c_str());
}

static std::vector<std::string> SplitByComma(const std::string& input) 
{
    std::vector<std::string> result;
    size_t start = 0;
    size_t end;

    while ((end = input.find(',', start)) != std::string::npos) {
        result.emplace_back(input.substr(start, end - start));
        start = end + 1;
    }

    // Add the last segment (or the whole string if no commas)
    result.emplace_back(input.substr(start));
    return result;
}

struct KeyBindState
{
    std::shared_ptr<CInput> input;
    bool waitingForInput = false;
    int processKeyCount = 0;            // we need to let the gui draw a frame or two before blocking for key input

    void Reset()
    {
        input = nullptr;
        waitingForInput = false;
        processKeyCount = 0;
    }
};

static void BindKeys(Util::Config::Node& config, KeyBindState& kb, bool openPopup)
{
    bool finish = false;
    bool appendPressed = false;

    if (openPopup) {
        ImGui::OpenPopup("Key Binding");
    }

    if (ImGui::BeginPopupModal("Key Binding", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

        if (ImGui::BeginTable("ShortcutTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {

            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("Keys");
            ImGui::TableHeadersRow();

            auto keyList = SplitByComma(kb.input->GetMapping());

            for (auto& k : keyList) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", kb.input->label);
                ImGui::TableNextColumn();
                ImGui::Text("%s", k.c_str());
            }

            ImGui::EndTable();
        }

        ImGui::Spacing(); // Adds default vertical spacing

        if (kb.waitingForInput) {

            ImVec4 color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // bright yellow
            color.w = 0.7f + 0.3f * 0.1f; // modulate alpha

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("Waiting for key input. Press esc to cancel.");
            ImGui::PopStyleColor();

            kb.processKeyCount++;
        }
        else {
            ImGui::NewLine();
        }
        ImGui::Spacing(); // Adds default vertical spacing


        if (kb.waitingForInput) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Set", ImVec2(120, 0))) {
            kb.input->ClearMapping();
            appendPressed = true;
        }

        if (ImGui::Button("Append", ImVec2(120, 0))) {
            appendPressed = true;
        }

        if (ImGui::Button("Clear", ImVec2(120, 0))) {
            kb.input->ClearMapping();
            kb.input->StoreToConfig(&config);
        }

        if (ImGui::Button("Default", ImVec2(120, 0))) {
            SetDefaultKeyVal(kb.input);
            kb.input->StoreToConfig(&config);
        }

        if (ImGui::Button("Finish", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            finish = true;
        }

        if (kb.waitingForInput) {
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();

        if (kb.processKeyCount > 2) {           // kind of cludge logic. We need to draw at least once to update the GUI, configure is a blocking function so the GUI will basically freeze until we press a button. Time out might make sense

            auto system = kb.input->GetInputSystem();

            system->UngrabMouse();
            kb.input->Configure(true);
            system->GrabMouse();

            kb.input->StoreToConfig(&config);
            kb.processKeyCount = 0;
            kb.waitingForInput = false;
        }

        if (appendPressed) {
            kb.waitingForInput = true;
        }

        if (finish) {
            kb.Reset();
        }
    }
}

static void AddKeys(Util::Config::Node& config, KeyBindState& kb, std::vector<std::shared_ptr<CInput>> keyInputs)
{
    bool openPopup = false;

    for (auto& k : keyInputs) {
        auto mapping    = k->GetMapping();
        auto group      = k->GetInputGroup();
        auto label      = k->label;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", group);
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                kb.input = k;
                openPopup = true;
            }
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%s", mapping);
    }

    BindKeys(config, kb, openPopup);
}

static void DrawButtonOptions(Util::Config::Node& config, int selectedGameIndex, bool& exit, bool& saveSettings)
{
    if (ImGui::Button("Load game")) {

        if (selectedGameIndex < 0) {
            ImGui::OpenPopup("Load game");
        }
        else {
            exit = true;
        }
    }

    if (ImGui::BeginPopupModal("Load game", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("No game selected");
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) { 
            ImGui::CloseCurrentPopup(); 
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Load Defaults")) {
        ImGui::OpenPopup("Confirm Load");
    }

    if (ImGui::BeginPopupModal("Confirm Load", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("Are you sure you want to load defaults?");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) { 
            config = DefaultConfig();
            ImGui::CloseCurrentPopup(); 
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) { 
            ImGui::CloseCurrentPopup(); 
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Exit")) {
        ImGui::OpenPopup("Save On Exit");
    }

    if (ImGui::BeginPopupModal("Save On Exit", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("Save settings upon exit?");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            selectedGameIndex = -1;
            saveSettings = true;
            exit = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("No", ImVec2(120, 0))) {
            selectedGameIndex = -1;
            saveSettings = false;
            exit = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // (The disabled "Per game settings" placeholder was removed: the top bar
    // only has Load game / Load Defaults / Exit.)
}

static std::shared_ptr<CInputs> GetInputSystem(Util::Config::Node& config, SDL_Window* window)
{
    std::string selectedInputSystem = config["InputSystem"].ValueAs<std::string>();

    std::shared_ptr<CInputSystem> inputSystem;

    if (selectedInputSystem == "sdl")               { inputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(config, false));}
    else if (selectedInputSystem == "sdlgamepad")   { inputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(config, true)); }

#ifdef SUPERMODEL_WIN32
    else if (selectedInputSystem == "dinput")       { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, false, false));}
    else if (selectedInputSystem == "xinput")       { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, false, true)); }
    else if (selectedInputSystem == "rawinput")     { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, true, false)); }
#endif // SUPERMODEL_WIN32

    // initialise 
    if (inputSystem) {
        auto inputs = std::shared_ptr<CInputs>(new CInputs(inputSystem));
        inputs->Initialize();
        inputs->LoadFromConfig(config); 

        int x, y, w, h;
        SDL_GetWindowPosition(window, &x, &y);
        SDL_GL_GetDrawableSize(window, &w, &h);

        inputSystem->SetDisplayGeom(x, y, w, h);
        return inputs;
    }

    return nullptr;
}

static bool RomZipExists(const std::string& name)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path("ROMs") / (name + ".zip"), ec);
}

// Empty if the game can be started, otherwise the zip file that is missing.
static std::string MissingRomZip(const Game& game, const std::set<std::string>& installed)
{
    if (!installed.count(game.name)) {
        return game.name + ".zip";
    }
    if (!game.parent.empty() && !installed.count(game.parent)) {
        return game.parent + ".zip (parent set of " + game.name + ")";
    }
    return {};
}

// Config::Node::Set() replaces the value, which drops the value range the
// settings tabs use to draw the control: keep it.
template <typename T>
static void SetKeepingRange(Util::Config::Node& config, const char* key, const T& value)
{
    std::shared_ptr<Util::ValueRange> range;
    if (Util::Config::Node* node = config.TryGet(key)) {
        if (auto v = node->GetValue())
            range = v->GetValueRange();
    }
    config.Set(key, value);
    if (range) {
        if (Util::Config::Node* node = config.TryGet(key)) {
            if (auto v = node->GetValue())
                v->SetValueRange(range);
        }
    }
}

// Resolution presets: the window size is what the 3D scene is drawn at; the
// Switch scales the window to the whole screen. A 16:9 window keeps the 4:3
// game area in proportion (black bars at the sides).
struct ResolutionPreset { const char* label; bool fullScreen; unsigned x, y; };
static const ResolutionPreset s_resolutionPresets[] = {
    { "1280x720 (best quality)",          true,  1280, 720 },
    { "960x540 (faster, 3D at 720x540)",  false,  960, 540 },
    { "854x480 (fastest, 3D at 640x480)", false,  854, 480 },
};

static void DrawResolutionPreset(Util::Config::Node& config)
{
    const unsigned x = config["XResolution"].ValueAs<unsigned>();
    const unsigned y = config["YResolution"].ValueAs<unsigned>();
    const int count = (int)(sizeof(s_resolutionPresets) / sizeof(s_resolutionPresets[0]));
    int current = -1;
    for (int i = 0; i < count; i++) {
        if (s_resolutionPresets[i].x == x && s_resolutionPresets[i].y == y)
            current = i;
    }

    char other[64];
    snprintf(other, sizeof(other), "%ux%u (custom)", x, y);
    const char* preview = current >= 0 ? s_resolutionPresets[current].label : other;

    ImGui::SetNextItemWidth(ImGui::CalcTextSize("960x540 (faster, 3D at 720x540)").x + 60.0f);
    if (ImGui::BeginCombo("Resolution", preview)) {
        for (int i = 0; i < count; i++) {
            if (ImGui::Selectable(s_resolutionPresets[i].label, i == current)) {
                SetKeepingRange(config, "FullScreen", s_resolutionPresets[i].fullScreen);
                SetKeepingRange(config, "XResolution", s_resolutionPresets[i].x);
                SetKeepingRange(config, "YResolution", s_resolutionPresets[i].y);
            }
            if (i == current)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

static Game GetGame(const std::map<std::string, Game>& games, int selectedGameIndex)
{
    Game game;

    if (selectedGameIndex >= 0) {

        int index = 0;
        for (const auto& g : games) {
            if (index == selectedGameIndex) {
                game = g.second;
                break;
            }
            index++;
        }
    }

    return game;
}

static void DrawGameList(const std::map<std::string, Game>& games, const std::set<std::string>& installed, int& selectedGameIndex, bool& exit, bool focus)
{
    // Fill the rest of the window with the list.
    ImGui::BeginChild("TableRegion", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::BeginTable("Games", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Title");
        ImGui::TableSetupColumn("Rom Name");
        ImGui::TableSetupColumn("Version");
        ImGui::TableSetupColumn("Year");
        ImGui::TableSetupColumn("Stepping");

        ImGui::TableHeadersRow();

        int row = 0;
        for (const auto& g : games) {

            const bool missing = !installed.count(g.second.name);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (missing) {
                ImGui::TextDisabled("%s", g.second.title.c_str());
            }
            else {
                ImGui::Text("%s", g.second.title.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            // Put the controller cursor on the selected game (or the first one).
            if (focus && (row == selectedGameIndex || (selectedGameIndex < 0 && row == 0))) {
                ImGui::SetKeyboardFocusHere();
            }
            if (ImGui::Selectable(g.second.name.c_str(), selectedGameIndex == row, ImGuiSelectableFlags_SpanAllColumns)) {
#ifdef __SWITCH__
                // Choosing a game (A) starts it straight away.
                exit = true;
#else
                // Pressing A on the game that is already selected starts it.
                if (selectedGameIndex == row) {
                    exit = true;
                }
#endif
                selectedGameIndex = row;
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                exit = true;
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", g.second.version.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", g.second.year);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%s", g.second.stepping.c_str());

            row++;
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

static void GUI(const ImGuiIO& io, Util::Config::Node& config, const std::map<std::string, Game>& games, const std::set<std::string>& installed, bool& onlyInstalled, int& selectedGameIndex, bool& exit, bool& saveSettings, SDL_Window* window, std::shared_ptr<CInputs>& inputs, KeyBindState& kb)
{
    ImVec4 clear_color = ImVec4(0.0f, 0.5f, 192/255.f, 1.00f);

    // Two screens: the game list (with Load game / Load Defaults / Exit on top)
    // and the settings, toggled with + on the controller (F1 on a keyboard).
    static bool showSettings = false;
    static bool focusPending = true;

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::Begin("Custom Window", nullptr, ImGuiWindowFlags_NoTitleBar); // Explicitly set a window name

    bool toggleSettings = false;
    const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (!popupOpen && !kb.waitingForInput &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) || ImGui::IsKeyPressed(ImGuiKey_F1, false))) {
        toggleSettings = true;
    }

    if (!showSettings) {

#ifndef __SWITCH__
        // draw button options
        DrawButtonOptions(config, selectedGameIndex, exit, saveSettings);

        // Right-aligned hint, also clickable for touch / mouse.
        const char* hint = "(+) Settings";
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(hint).x);
        ImGui::TextDisabled("%s", hint);
        if (ImGui::IsItemClicked()) {
            toggleSettings = true;
        }

        ImGui::Spacing();
#endif
        // Switch: only the game list (+ opens the settings, HOME closes the program).

        DrawGameList(games, installed, selectedGameIndex, exit, focusPending);
        focusPending = false;

        // Don't leave the menu for a game whose ROMs aren't there: say so instead.
        static std::string missingZip;
        if (exit && selectedGameIndex >= 0) {
            missingZip = MissingRomZip(GetGame(games, selectedGameIndex), installed);
            if (!missingZip.empty()) {
                exit = false;
                ImGui::OpenPopup("ROM not found");
            }
        }

        if (ImGui::BeginPopupModal("ROM not found", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::Text("This game is not on the SD card.");
            ImGui::Text("Missing file: ROMs/%s", missingZip.c_str());
            ImGui::Separator();

            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();

            ImGui::EndPopup();
        }

    }
    else {

        if (focusPending) {
            ImGui::SetKeyboardFocusHere();
            focusPending = false;
        }
        if (ImGui::Button("Back")) {
            toggleSettings = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(+) Back to game list");

        DrawResolutionPreset(config);

        ImGui::Spacing();

        // draw the tabbed options
        if (ImGui::BeginTabBar("MyTabBar", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            if (ImGui::BeginTabItem("General")) {
                if (ImGui::Checkbox("Only show games found in ROMs folder", &onlyInstalled)) {
                    selectedGameIndex = -1;     // row numbers refer to the other list now
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%d found)", (int)installed.size());
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            if (ImGui::BeginTabItem("Core")) {
                UpdateTempValues(config, "Core", true);
                CreateControls(config, "Core");
                UpdateTempValues(config, "Core", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            if (ImGui::BeginTabItem("Video")) {
                UpdateTempValues(config, "Video", true);
                CreateControls(config, "Video");
                UpdateTempValues(config, "Video", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            if (ImGui::BeginTabItem("Audio")) {
                UpdateTempValues(config, "Sound", true);
                CreateControls(config, "Sound");
                UpdateTempValues(config, "Sound", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
#ifndef __SWITCH__
            // Switch: only Core, Video and Audio are shown.
            if (ImGui::BeginTabItem("Networking")) {
                UpdateTempValues(config, "Network", true);
                CreateControls(config, "Network");
                UpdateTempValues(config, "Network", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            if (ImGui::BeginTabItem("Misc")) {
                UpdateTempValues(config, "Misc", true);
                CreateControls(config, "Misc");
                UpdateTempValues(config, "Misc", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            if (ImGui::BeginTabItem("ForceFeedback")) {
                UpdateTempValues(config, "ForceFeedback", true);
                CreateControls(config, "ForceFeedback");
                UpdateTempValues(config, "ForceFeedback", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            if (ImGui::BeginTabItem("Sensitivity")) {
                UpdateTempValues(config, "Sensitivity", true);
                CreateControls(config, "Sensitivity");
                UpdateTempValues(config, "Sensitivity", false);
                if (ImGui::Button("Joystick calibration")) {
                    if (inputs == nullptr) {
                        if (inputs == nullptr) {
                            inputs = GetInputSystem(config, window);
                        }
                    }
                    inputs->CalibrateJoysticks();
                    inputs->StoreToConfig(&config);
                }
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            if (ImGui::BeginTabItem("Key bindings")) {

                if (inputs == nullptr) {
                    inputs = GetInputSystem(config, window);
                }

                auto inputList = inputs->GetGameInputs(GetGame(games,selectedGameIndex));

                if (ImGui::BeginTable("KeyTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {

                    ImGui::TableSetupColumn("Group");
                    ImGui::TableSetupColumn("Action");
                    ImGui::TableSetupColumn("Keys");
                    ImGui::TableHeadersRow();

                    AddKeys(config, kb, inputList);

                    ImGui::EndTable();
                }
                
                ImGui::EndTabItem();
            }

#endif

            ImGui::EndTabBar();
        }

    }

    if (toggleSettings) {
        showSettings = !showSettings;
        focusPending = true;
        inputs = nullptr;
        kb.Reset();
    }

    ImGui::End(); // Close the window


    // Rendering
    ImGui::Render();
    
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static std::string GetRomPath(int selectedGame, const std::map<std::string, Game>& games)
{
    if (selectedGame >= 0) {
        int index = 0;
        for (auto& g : games) {
            if (selectedGame == index) {
                return (std::filesystem::path("ROMs") / (g.second.name + ".zip")).string();        // todo config rom directory? File dialog will be a bit more tricky cross platform but we can specifiy edit box for manual path entry        
            }
            index++;
        }
    }

    return {};  // no game
}

static float GetDPIScale(SDL_Window* window)
{
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    float ddpi = 96.0f; // Default fallback
    if (SDL_GetDisplayDPI(displayIndex, &ddpi, nullptr, nullptr) != 0) {
        SDL_Log("Failed to get DPI: %s", SDL_GetError());
    }

    return ddpi / 96.0f;
}

std::vector<std::string> RunGUI(const std::string& configPath, Util::Config::Node& config)
{
    // Initialize SDL
#ifdef __SWITCH__
    // The Joy-Cons / Pro Controller drive the menu (ImGui gamepad navigation).
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
#else
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
#endif
        std::cerr << "SDL could not initialize! Error: " << SDL_GetError() << std::endl;
        return {};
    }

    // Set OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#ifdef __SWITCH__
    // Must match CreateGLScreen() in Main.cpp exactly: the emulator takes this
    // window and context over, and SDL rebuilds the EGL surface from the
    // current attributes, which must give the context's EGL config.
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
#endif
#ifdef __SWITCH__
    // Same flags as the emulator's own window, which takes this one over. A
    // resizable window would also be resized by SDL on every dock/undock.
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS);
#else
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_INPUT_FOCUS);
#endif

    // Create SDL window
#ifdef __SWITCH__
    SDL_Window* window = SDL_CreateWindow("SuperSetup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
#else
    SDL_Window* window = SDL_CreateWindow("SuperSetup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 950, 600, window_flags);
#endif
    if (!window) {
        std::cerr << "Window could not be created! Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return {};
    }

    // Create OpenGL context
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "OpenGL context could not be created! Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return {};
    }

    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1); // Enable vsync

#ifdef __SWITCH__
    // No system libGL: load the GL functions for this context (glad).
    if (glewInit() != GLEW_OK) {
        std::cerr << "Unable to load OpenGL functions" << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return {};
    }
    SwitchAddGamepadMappings();
#endif

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    io.Fonts->AddFontDefaultVector();

    ImGui::GetIO().IniFilename = nullptr;                      // we don't need to save window positions between runs

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();
    //ImGui::StyleColorsClassic();

    ImGuiStyle& style = ImGui::GetStyle();
    float scale = GetDPIScale(window);
    //style.ScaleAllSizes(scale);
#ifdef __SWITCH__
    // Readable on a TV and on the handheld screen.
    style.FontScaleMain = 1.5f;
    style.ScaleAllSizes(1.5f);
    (void)scale;
#endif

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 410");

    std::string xmlFile = config["GameXMLFile"].ValueAs<std::string>();
    GameLoader loader(xmlFile);
    const auto& allGames = loader.GetGames();

    // ROM sets present in the ROMs folder (the path GetRomPath() builds).
    // Checked once here: the SD card is slow to query every frame.
    std::set<std::string> installed;
    std::map<std::string, Game> installedGames;
    for (const auto& g : allGames) {
        if (RomZipExists(g.second.name)) {
            installed.insert(g.second.name);
            installedGames.insert(g);
        }
    }
    bool onlyInstalled = !installedGames.empty();
    auto currentList = [&]() -> const std::map<std::string, Game>& {
        return onlyInstalled ? installedGames : allGames;
    };
    int selectedGame = -1;  // -1 means no selection
    std::vector<std::string> romFiles;
    std::string path;

    // Main loop
    std::shared_ptr<CInputs> inputs;
    bool saveSettings = true;
    bool running = true;
    KeyBindState kb{};
    bool exit = false;
    SDL_Event event{};

    while (running) {

        while (SDL_PollEvent(&event)) {

            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                goto exitNoSave;
            }
        }

        GUI(io, config, currentList(), installed, onlyInstalled, selectedGame, exit, saveSettings, window, inputs, kb);

        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        SDL_GL_SwapWindow(window);

        if (exit) {
            break;
        }
    }

    path = GetRomPath(selectedGame, currentList());
    if (!path.empty()) {
        romFiles.emplace_back(path);
    }
    
    if (saveSettings) {
        Util::Config::WriteINIFile(configPath, config, "");
    }

exitNoSave:

    // Cleanup resources
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

#ifdef __SWITCH__
    if (!romFiles.empty()) {
        // Keep the window and context for the emulator (see Gui.h).
        s_guiWindow = window;
        s_guiContext = glContext;
        return romFiles;
    }
#endif
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return romFiles;
}

#ifdef __SWITCH__
SDL_Window *TakeGuiWindow(void **glContext)
{
    SDL_Window *window = s_guiWindow;
    *glContext = s_guiContext;
    s_guiWindow = nullptr;
    s_guiContext = nullptr;
    return window;
}
#endif


