#include <SDL.h>
#include "main.h"
#include "Core.h"
#include "SDL_UI.h"

#define IMGUI

int main(int argc, char* argv[]) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
		MessageBoxA(NULL, "BlueEmu failed to initialize COM library.", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }
#ifdef IMGUI
    Core core;
    if (core.Initialize() == S_OK && core.CreateWindows() == S_OK)
    {
        core.RunMessageLoop();
    }
#else
    SDL_UI sdl_ui;

    if (sdl_ui.Initialize() == S_OK && sdl_ui.CreateWindows() == S_OK)
    {
        sdl_ui.RunMessageLoop();
    }
#endif

    return 0;
}