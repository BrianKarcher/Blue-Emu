#pragma once
#include <Windows.h>
#include <malloc.h>
#include <memory.h>
#include <wchar.h>
#include <math.h>
#include <array>
#include "bus.h"
#include "PPU.h"
#include "CPU.h"
#include "APU.h"
#include "Input.h"
#include "PPUViewer.h"
#include "EmulatorCore.h"
#include "SharedContext.h"
#include "DebuggerUI.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include "HexViewer.h"

template<class Interface>
inline void SafeRelease(
	Interface** ppInterfaceToRelease
)
{
	if (*ppInterfaceToRelease != NULL)
	{
		(*ppInterfaceToRelease)->Release();
		(*ppInterfaceToRelease) = NULL;
	}
}

#ifndef Assert
#if defined( DEBUG ) || defined( _DEBUG )
#define Assert(b) do {if (!(b)) {OutputDebugStringA("Assert: " #b "\n");}} while (0)
#else
#define Assert(b)
#endif //DEBUG || _DEBUG
#endif

#ifndef HINST_THISCOMPONENT
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

class DebuggerContext;

class Core
{
public:
	Core();
	bool init();
	SDL_GLContext gl_context;
	ImGuiIO io;
	SharedContext context;
	EmulatorCore emulator;
	PPUViewer ppuViewer;
	
	// Register the window class and call methods for instantiating drawing resources
	HRESULT Initialize();
	HRESULT CreateWindows();
	// Load the game ROM
	void LoadGame(
		const std::string& szFileName
	);

	HWND GetWindowHandle();
	// Process and dispatch messages
	void RunMessageLoop();

	void (*Update)();
	// Hex dump window
	uint8_t* g_buffer;
	size_t g_bufferSize;
	void UpdateScrollInfo(HWND hwnd);
	void RecalcLayout(HWND hwnd);
	void DrawHexDump(HDC hdc, RECT const& rc);
	HFONT hFont = nullptr;
	HDC memDC = nullptr;
	HBITMAP memBitmap = nullptr;
	HBITMAP oldBitmap = nullptr;
	int bufferWidth = 0, bufferHeight = 0;
	int lineHeight = 16;
	HWND m_hwndPalette;
	bool isPlaying = false;

	HWND hHexCombo = NULL;
	HWND hHexDrawArea = NULL;
	int hexView = 0;
	DebuggerUI debuggerUI;
	HexViewer hexViewer;
	//PPUViewer ppuViewer;
private:
	GLuint nes_texture;
	GLuint pbo;
	HMENU hMenu;
	Bus* _bus;
	DebuggerContext* _dbgCtx;
	void updateMenu();
	bool RenderFrame(const uint32_t* frame_data);
	bool ClearFrame();
	void DrawGameCentered();

	void PollControllerState();
	bool PollSDLEvents();
	bool isPaused;
	std::string lastOpenedPath = ".";

	// Resize the render target.
	void OnResize(
		UINT width,
		UINT height
	);
	
	HWND m_hwnd;
	struct UIWindows {
		bool ppuOpen = true;
		bool cpuOpen = true;
		bool hexOpen = true;
		bool debuggerOpen = true;
	};

	UIWindows _uiWindows;
};