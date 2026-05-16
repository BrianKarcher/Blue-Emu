#pragma once
#include <array>
#include <SDL_opengl.h>
#include "imgui.h"

class Core;
class NesBus;
class NesPpu;
class SharedContext;
class NesCartridge;
class DebuggerContext;

class NesPpuViewer
{
public:
	bool Initialize(Core* core, SharedContext* sharedCtx);
	void DrawNametables();
	void Draw(const char* title, bool* p_open);

private:
	GLuint ntTextures[4] = { 0, 0, 0, 0 };
	GLuint chr_textures[4] = { 0, 0, 0, 0 };
	GLuint oam_texture = 0;
	GLuint sprite_texture = 0;

	void CreateTexture(GLuint& id, int width, int height);
	uint16_t GetBaseNametableAddress();
	void DrawWrappedRect(ImDrawList* draw_list, ImVec2 canvas_p0, float x, float y);
	void UpdateCHRTexture(int bank, uint8_t palette_idx);
	void DrawCHRViewer();
	void DrawOAMViewer();
	
	void UpdateTexture(int index, std::array<uint32_t, 256 * 240>& data);
	void renderNametable(std::array<uint32_t, 256 * 240>& buffer, int physicalTable);
	void get_palette_index_from_attribute(uint8_t attributeByte, int tileRow, int tileCol, uint8_t& paletteIndex);
	void render_tile(std::array<uint32_t, 256 * 240>& buffer,
		int pr, int pc, int tileIndex, std::array<uint32_t, 4>& colors);

	std::array<std::array<uint32_t, 256 * 240>, 4> *nt;
	std::array<uint32_t, 64 * 64> *_oam;
	std::array<uint32_t, 256 * 240> *_sprites;
	Core* _core;
	NesCartridge* _cartridge;
	NesPpu* _ppu;
	SharedContext* _sharedContext;
	DebuggerContext* _dbgContext;
};