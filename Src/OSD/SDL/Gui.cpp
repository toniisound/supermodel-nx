#include "SDLIncludes.h"
#include <GL/glew.h>
#include <cstring>
#include <cstdio>
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
#include <deque>

// Game covers: PNG and JPEG decoding (stb_image, public domain / MIT)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO_WARNINGS
#include "../../Pkgs/stb_image.h"

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

// Name shown in the settings for a config key. The key itself (the name in
// Supermodel.ini) never changes.
static std::string DisplayName(const std::string& key)
{
#ifdef __SWITCH__
    if (key == "ShowFrameRate")   return "Write FPS to Supermodel.log";
    if (key == "ShowFPSOnScreen") return "Show FPS on screen";
#endif
    return key;
}

static void CreateControls(Util::Config::Node& config, const std::string group)
{
    for (auto it = config.begin(); it != config.end(); ++it)
    {
        if (it->IsLeaf() && it->Exists()) {

            auto key = it->Key();
            auto val = it->GetValue();

#ifdef __SWITCH__
            // The resolution is chosen with the "Resolution" selector above the tabs.
            if (key == "XResolution" || key == "YResolution")
                continue;
            // Core tab: only the emulated CPU speed, drawn by DrawPowerPCFrequency();
            // the rest stays as the .ini sets it.
            if (group == "Core")
                continue;
#endif

            // Text shown next to the control ("##key" keeps the ImGui ID unique)
            const std::string label = DisplayName(key) + "##" + key;

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

                            ImGui::Combo(label.c_str(), &selectedIndex, sVectorChar.data(), (int)sVectorChar.size());
                            vRange->tempValue = list[selectedIndex];
                        };

                        auto ProcessScalar = [&](auto valuePtr, ImGuiDataType type)
                        {
                            using T = std::decay_t<decltype(*valuePtr)>;
                            auto min_ = std::get<T>(vRange->GetMin());
                            auto max_ = std::get<T>(vRange->GetMax());
                            ImGui::SliderScalar(label.c_str(), type, valuePtr, &min_, &max_);
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
                                ImGui::InputScalar(label.c_str(), type, valuePtr);
                            }
                        };


                        switch (index) {
                        case 0:             // bool
                        {
                            auto p = std::get_if<bool>(&vRange->tempValue);
                            ImGui::Checkbox(label.c_str(), p);
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

                                ImGui::Combo(label.c_str(), &selectedIndex, sVector.data(), (int)sVector.size());
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

// "Load Defaults". On the Switch the defaults are the Supermodel.ini bundled
// in the .nro (Joy-Con controls, sdlgamepad, 1280x720...) over the built-in
// ones; the built-in ones alone would leave keyboard controls.
static void LoadDefaultSettings(Util::Config::Node& config)
{
#ifdef __SWITCH__
    const char* tmp = "Config/Supermodel.default.ini";
    if (SwitchCopyBundledConfig(tmp)) {
        Util::Config::Node bundled("Global");
        Util::Config::Node merged("Global");
        Util::Config::FromINIFile(&bundled, tmp);
        std::remove(tmp);
        Util::Config::MergeINISections(&merged, DefaultConfig(), bundled);
        config = merged;
        return;
    }
#endif
    config = DefaultConfig();
}

// PowerPC frequency: automatic (each board's real speed) or one of the boards' speeds.
struct FrequencyPreset { const char* label; unsigned mhz; };
static const FrequencyPreset s_frequencyPresets[] = {
    { "Auto (each game at its board's speed)",               0 },
    { "66 MHz (Step 1.0: Virtua Fighter 3, Scud Race)",     66 },
    { "100 MHz (Step 1.5: Le Mans 24, Virtua Fighter 3 tb)", 100 },
    { "166 MHz (Step 2.x: Daytona 2, Sega Rally 2)",        166 },
};

static void DrawPowerPCFrequency(Util::Config::Node& config)
{
    const unsigned mhz = config["PowerPCFrequency"].ValueAsDefault<unsigned>(0);
    const int count = (int)(sizeof(s_frequencyPresets) / sizeof(s_frequencyPresets[0]));
    int current = -1;
    for (int i = 0; i < count; i++) {
        if (s_frequencyPresets[i].mhz == mhz)
            current = i;
    }

    char other[64];
    snprintf(other, sizeof(other), "%u MHz (custom)", mhz);
    const char* preview = current >= 0 ? s_frequencyPresets[current].label : other;

    ImGui::SetNextItemWidth(ImGui::CalcTextSize(s_frequencyPresets[2].label).x + 60.0f);
    if (ImGui::BeginCombo("PowerPC frequency", preview)) {
        for (int i = 0; i < count; i++) {
            if (ImGui::Selectable(s_frequencyPresets[i].label, i == current))
                SetKeepingRange(config, "PowerPCFrequency", s_frequencyPresets[i].mhz);
            if (i == current)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

#ifdef __SWITCH__
// Header of the game list: the app logo and the "(+) Settings" hint. The logo
// is only read from inside the .nro (RomFS, from Assets/logo.bmp in the
// project; any size, 32-bit BMP for transparency, scaled to the header
// height). Without it the title is written instead.
static const char* kLogoPath = "romfs:/Assets/logo.bmp";
static GLuint s_logoTexture = 0;
static int s_logoWidth = 0, s_logoHeight = 0;
static bool s_logoTried = false;

static void LoadLogo()
{
    s_logoTried = true;
    std::vector<unsigned char> file;
    if (!SwitchReadRomfsFile(kLogoPath, file))
        return;
    SDL_Surface* bmp = SDL_LoadBMP_RW(SDL_RWFromConstMem(file.data(), (int)file.size()), 1);
    if (!bmp)
        return;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(bmp, SDL_PIXELFORMAT_ABGR8888, 0);  // R,G,B,A bytes
    SDL_FreeSurface(bmp);
    if (!rgba)
        return;

    glGenTextures(1, &s_logoTexture);
    glBindTexture(GL_TEXTURE_2D, s_logoTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    s_logoWidth = rgba->w;
    s_logoHeight = rgba->h;
    SDL_FreeSurface(rgba);
}

static void FreeLogo()
{
    if (s_logoTexture)
        glDeleteTextures(1, &s_logoTexture);
    s_logoTexture = 0;
    s_logoTried = false;
}

// Game covers, provided by the player: Covers/<ROM set>.png (or .jpg/.jpeg)
// on the SD card, e.g. Covers/daytona2.png. A clone without its own cover
// uses its parent's. None are included with the program.
struct Cover {
    GLuint texture = 0;     // 0: no cover for this set
    int width = 0, height = 0;
};
static std::map<std::string, Cover> s_covers;     // tried sets (with or without a cover)
static std::deque<std::string> s_coverOrder;      // loaded textures, oldest first
static const size_t kMaxCoverTextures = 12;
static std::string s_coverGame, s_coverParent;    // game under the list cursor

static Cover LoadCover(const std::string& set)
{
    Cover cover;
    for (const char* ext : { ".png", ".jpg", ".jpeg", ".PNG", ".JPG" }) {
        const std::string path = std::string(SWITCH_SUPERMODEL_ROOT "/Covers/") + set + ext;
        int w, h, n;
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!pixels)
            continue;

        // Halve big pictures (box filter) so a cover stays a few MB at most.
        while (w > 1024 || h > 1024) {
            const int nw = w / 2, nh = h / 2;
            for (int y = 0; y < nh; y++) {
                for (int x = 0; x < nw; x++) {
                    const unsigned char* a = pixels + ((2 * y) * w + 2 * x) * 4;
                    const unsigned char* b = a + w * 4;
                    unsigned char* d = pixels + (y * nw + x) * 4;
                    for (int c = 0; c < 4; c++)
                        d[c] = (unsigned char)((a[c] + a[c + 4] + b[c] + b[c + 4] + 2) / 4);
                }
            }
            w = nw; h = nh;
        }

        glGenTextures(1, &cover.texture);
        glBindTexture(GL_TEXTURE_2D, cover.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(pixels);
        cover.width = w;
        cover.height = h;
        break;
    }
    return cover;
}

static const Cover& GetCover(const std::string& set)
{
    auto it = s_covers.find(set);
    if (it != s_covers.end())
        return it->second;

    // Keep a few textures loaded, so going back up the list is instant.
    Cover cover = LoadCover(set);
    if (cover.texture) {
        s_coverOrder.push_back(set);
        if (s_coverOrder.size() > kMaxCoverTextures) {
            auto old = s_covers.find(s_coverOrder.front());
            if (old != s_covers.end()) {
                glDeleteTextures(1, &old->second.texture);
                s_covers.erase(old);    // tried again if it comes back
            }
            s_coverOrder.pop_front();
        }
    }
    return s_covers[set] = cover;
}

static void FreeCovers()
{
    for (auto& c : s_covers) {
        if (c.second.texture)
            glDeleteTextures(1, &c.second.texture);
    }
    s_covers.clear();
    s_coverOrder.clear();
}

static void DrawCoverPanel(const ImVec2& size)
{
    ImGui::BeginChild("Cover", size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    const Cover* cover = nullptr;
    if (!s_coverGame.empty()) {
        cover = &GetCover(s_coverGame);
        if (!cover->texture && !s_coverParent.empty())
            cover = &GetCover(s_coverParent);
    }

    if (cover && cover->texture) {
        // Fit the cover in the panel, keeping its proportions, centred
        float w = avail.x;
        float h = w * float(cover->height) / float(cover->width);
        if (h > avail.y) { h = avail.y; w = h * float(cover->width) / float(cover->height); }
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (avail.x - w) * 0.5f, ImGui::GetCursorPosY() + (avail.y - h) * 0.5f));
        ImGui::Image((ImTextureID)(intptr_t)cover->texture, ImVec2(w, h));
    }
    else if (!s_coverGame.empty()) {
        const char* text = "No cover";
        const std::string hint = "Covers/" + s_coverGame + ".png";
        const float lineH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - lineH * 2.0f) * 0.5f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - ImGui::CalcTextSize(text).x) * 0.5f);
        ImGui::TextDisabled("%s", text);
        ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x + std::max(0.0f, (avail.x - ImGui::CalcTextSize(hint.c_str()).x) * 0.5f));
        ImGui::TextDisabled("%s", hint.c_str());
    }
    ImGui::EndChild();
}

static void DrawHeader(bool& toggleSettings)
{
    if (!s_logoTried)
        LoadLogo();

    // Logo, centred
    const float headerHeight = 130.0f;
    ImGui::BeginChild("Header", ImVec2(0.0f, headerHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    const float avail = ImGui::GetContentRegionAvail().x;

    if (s_logoTexture) {
        // Fit the logo in the header, keeping its proportions
        const float maxW = avail * 0.9f;
        float h = headerHeight - 10.0f;
        float w = h * float(s_logoWidth) / float(s_logoHeight);
        if (w > maxW) { w = maxW; h = w * float(s_logoHeight) / float(s_logoWidth); }
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (avail - w) * 0.5f, (headerHeight - h) * 0.5f));
        ImGui::Image((ImTextureID)(intptr_t)s_logoTexture, ImVec2(w, h));
    }
    else {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
        const ImVec2 size = ImGui::CalcTextSize("SuperModel NX");
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (avail - size.x) * 0.5f, (headerHeight - size.y) * 0.5f));
        ImGui::TextUnformatted("SuperModel NX");
        ImGui::PopFont();
    }
    ImGui::EndChild();

    // Separate text line under the logo, on the right
    const char* hint = "(+) Settings";
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(hint).x);
    ImGui::TextDisabled("%s", hint);
    if (ImGui::IsItemClicked())
        toggleSettings = true;
}
#endif

#ifdef __SWITCH__
static void DrawCredits()
{
    ImGui::BeginChild("CreditsText", ImVec2(0.0f, 0.0f), false);

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted("SuperModel NX 1.2.1");
    ImGui::PopFont();
    ImGui::TextUnformatted("Nintendo Switch fork by ToniiSound and Thorhax");

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Based on");
    ImGui::BulletText("Supermodel - A Sega Model 3 Arcade Emulator");
    ImGui::Indent();
    ImGui::TextWrapped("Copyright 2003-2025 The Supermodel Team (Bart Trzynadlowski, Nik Henson, "
                       "Ian Curtis and contributors). supermodel3.com");
    ImGui::Unindent();
    ImGui::BulletText("Libretro-Supermodel");
    ImGui::Indent();
    ImGui::TextWrapped("libretro and sgiannop/Libretro-Supermodel: modernized code base and the "
                       "ARM64 PowerPC recompiler this fork is built on.");
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::TextDisabled("Libraries and tools");
    ImGui::BulletText("devkitPro and libnx (Switch homebrew toolchain and library)");
    ImGui::BulletText("SDL2, Mesa (OpenGL), glad");
    ImGui::BulletText("Dear ImGui by Omar Cornut");
    ImGui::BulletText("Musashi 68000 core by Karl Stenerud");
    ImGui::BulletText("zlib and minizip");
    ImGui::BulletText("stb_image by Sean Barrett (game covers)");
    ImGui::BulletText("Logo set in Bungee Inline by David Jonathan Ross (SIL Open Font License)");

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextWrapped("Free software distributed under the GNU General Public License, version 3 "
                       "or later, with no warranty. Its source code is available under the same license.");
    ImGui::TextWrapped("Sega, Model 3 and the game titles are trademarks of their owners. No ROMs are "
                       "included; use only games you own.");

    ImGui::EndChild();
}
#endif

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
#ifdef __SWITCH__
    // List on the left, cover of the game under the cursor on the right.
    const float coverWidth = std::floor(ImGui::GetContentRegionAvail().x * 0.30f);
    const float listWidth = ImGui::GetContentRegionAvail().x - coverWidth - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("TableRegion", ImVec2(listWidth, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);   // a bit smaller, so the columns fit
#else
    // Fill the rest of the window with the list.
    ImGui::BeginChild("TableRegion", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
#endif

    if (ImGui::BeginTable("Games", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("TITLE");
        ImGui::TableSetupColumn("ROM NAME");
        ImGui::TableSetupColumn("VERSION");

#ifdef __SWITCH__
        // Plain header row: labels only, so the controller cursor can't land on them.
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int column = 0; column < ImGui::TableGetColumnCount(); column++) {
            ImGui::TableSetColumnIndex(column);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
            ImGui::TextUnformatted(ImGui::TableGetColumnName(column));
        }
#else
        ImGui::TableHeadersRow();
#endif

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
#ifdef __SWITCH__
            if (ImGui::IsItemFocused() || ImGui::IsItemHovered() ||
                (s_coverGame.empty() && row == std::max(selectedGameIndex, 0))) {
                s_coverGame = g.second.name;
                s_coverParent = g.second.parent;
            }
#endif

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                exit = true;
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", g.second.version.c_str());

            row++;
        }

        ImGui::EndTable();
    }

#ifdef __SWITCH__
    ImGui::PopFont();
#endif
    ImGui::EndChild();

#ifdef __SWITCH__
    ImGui::SameLine();
    DrawCoverPanel(ImVec2(coverWidth, 0.0f));
#endif
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
#ifdef __SWITCH__
        // Switch: logo header and the game list (+ opens the settings, HOME closes the program).
        DrawHeader(toggleSettings);
#endif

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

        ImGui::Spacing();

        // draw the tabbed options
#ifdef __SWITCH__
        // The five tabs (General, Core, Video, Audio, Credits) share the full width.
        const int tabCount = 5;
        const float tabWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x * (tabCount - 1)) / tabCount;
        auto FullWidthTab = [tabWidth]() { ImGui::SetNextItemWidth(tabWidth); };
#else
        auto FullWidthTab = []() {};
#endif
        if (ImGui::BeginTabBar("MyTabBar", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            FullWidthTab();
            if (ImGui::BeginTabItem("General")) {
                UpdateTempValues(config, "General", true);
                CreateControls(config, "General");      // ShowFPSOnScreen
                UpdateTempValues(config, "General", false);
                if (ImGui::Checkbox("Only show games found in ROMs folder", &onlyInstalled)) {
                    selectedGameIndex = -1;     // row numbers refer to the other list now
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%d found)", (int)installed.size());

                ImGui::Spacing();
                if (ImGui::Button("Load Defaults")) {
                    ImGui::OpenPopup("Confirm Load Defaults");
                }
                if (ImGui::BeginPopupModal("Confirm Load Defaults", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Restore the default settings (video, audio, core and controls)?");
                    ImGui::Separator();
                    if (ImGui::Button("Yes", ImVec2(120, 0))) {
                        LoadDefaultSettings(config);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::EndPopup();
                }
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            FullWidthTab();
            if (ImGui::BeginTabItem("Core")) {
#ifdef __SWITCH__
                DrawPowerPCFrequency(config);       // before UpdateTempValues(), like the resolution
#endif
                UpdateTempValues(config, "Core", true);
                CreateControls(config, "Core");
                UpdateTempValues(config, "Core", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            FullWidthTab();
            if (ImGui::BeginTabItem("Video")) {
                // Before UpdateTempValues(): a preset change must not be
                // overwritten by the tab's values from before the change.
                DrawResolutionPreset(config);
                UpdateTempValues(config, "Video", true);
                CreateControls(config, "Video");
                UpdateTempValues(config, "Video", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
            FullWidthTab();
            if (ImGui::BeginTabItem("Audio")) {
                UpdateTempValues(config, "Sound", true);
                CreateControls(config, "Sound");
                UpdateTempValues(config, "Sound", false);
                ImGui::EndTabItem();
                inputs = nullptr;
            }
#ifdef __SWITCH__
            FullWidthTab();
            if (ImGui::BeginTabItem("Credits")) {
                DrawCredits();
                ImGui::EndTabItem();
                inputs = nullptr;
            }
#endif
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
#ifdef __SWITCH__
    FreeLogo();
    FreeCovers();
#endif
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


