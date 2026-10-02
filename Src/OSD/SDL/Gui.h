#ifndef _GUI_H_
#define _GUI_H_

#include <string>
#include <vector>

std::vector<std::string> RunGUI(const std::string& configPath, Util::Config::Node& config);

#ifdef __SWITCH__
// On the Switch the game selection window and its OpenGL context are kept
// open and handed to the emulator instead of being destroyed and created
// again (some Switch emulators fail when the GPU channel is closed and
// reopened, and it saves time on hardware). Returns nullptr if there is none;
// ownership passes to the caller.
struct SDL_Window;
SDL_Window *TakeGuiWindow(void **glContext);
#endif

#endif // !_GUI_H_
