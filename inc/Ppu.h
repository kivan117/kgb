#pragma once
#include <iostream>
#include <array>
#include "Mmu.h"
#include <SDL2\SDL.h>
class Ppu
{
public:
	Ppu(Mmu* __mmu, SDL_Texture* tex, SDL_Renderer* rend);
	void Tick(uint16_t cycles);
	uint8_t* GetFramebuffer();
	uint32_t* GetColorFrameBuffer();
	bool newFrame{ true };
private:
	Mmu* mmu;
	uint64_t PpuCycles{ 0 };
	uint64_t PpuTotalCycles{ 0 };
	uint8_t currentMode{ 2 };
	uint8_t currentLine{ 0 };
	uint8_t windowCounter{ 0 };
	bool windowLYTrigger{ false };

	const uint16_t OAM_CYCLES{ 80 };
	uint16_t DRAW_CYCLES{ 172 };
	
	//lcdc control bit names
	const uint8_t BG_ENABLE{ 0x01 };
	const uint8_t CGB_BG_PRIORITY{ 0x01 };
	const uint8_t SPRITE_ENABLE{ 0x02 };
	const uint8_t TALL_SPRITE_ENABLE{ 0x04 };
	const uint8_t WINDOW_ENABLE{ 0x20 };
	const uint8_t LCD_ENABLE{ 0x80 };

	//stat interrupt control bit names
	const uint8_t STAT_HBLANK_ENABLE{ 0x08 };
	const uint8_t STAT_VBLANK_ENABLE{ 0x10 };
	const uint8_t STAT_OAM_ENABLE{ 0x20 };
	const uint8_t STAT_LYC_ENABLE{ 0x40 };

	bool statIntAvail{ true };

	void SetMode(uint8_t mode);
	void SetLine(uint8_t line);
	void CheckLYC();

	void RenderLine();

	void RenderLineDMG();

	void RenderLineCGB();

	void RenderFrame();

	void SpriteSearch();

	uint8_t FrameBuffer[160 * 144] = { 0 };
	uint8_t WorkingFrameBuffer[160 * 144] = { 0 };

	uint32_t ColorFrameBuffer[160 * 144] = { 0 };
	uint32_t WorkingColorFrameBuffer[160 * 144] = { 0 };

	struct Sprite {
		uint8_t index{ 10 };
		uint8_t x{ 255 };
		uint8_t y{ 255 };
		bool xflip{ false };
		bool yflip{ false };
		bool bg_priority{ false };
		uint8_t palette{ 0 };
		uint8_t colorpalette{ 0 };
		uint8_t tile_id{ 0 };
		uint8_t vram_bank{ 0 };

		inline bool operator <(const Sprite& other) const { if (x != other.x) { return x < other.x; } return index < other.index; };

	};
	
	std::array<Sprite, 40> allSprites;

	std::array<Sprite, 10> lineSprites;

	std::array<uint8_t, 160> bgPixelPriority;

	uint8_t lineSpriteCount{ 0 };

	bool isLCDOn{ true };

	bool blendFrames{ false };

	SDL_Texture* ppuTexture{ nullptr };
	SDL_Renderer* ppuRenderer{ nullptr };

	const uint32_t palette_gbp_gray[4] = { 0xE0DBCDFF, 0xA89F94FF, 0x706B66FF, 0x2B2B26FF };
	const uint32_t palette_gbp_green[4] = { 0xDBF4B4FF, 0xABC396FF, 0x7B9278FF, 0x4C625AFF };
	const uint32_t palette_platinum[4] = { 0xE0F0E8FF, 0xA8C0B0FF, 0x507868FF, 0x183030FF };
	const uint32_t palette_luxa[4] = { 0xE6E6FFFF, 0xBEBEE6FF, 0x50506EFF, 0x1E1E3CFF };
	const uint32_t palette_bgb[4] = { 0xE0F8D0FF, 0x88C070FF, 0x346856FF, 0x081820FF };
	const uint32_t palette_mist[4] = { 0xC4F0C2FF, 0x5AB9A8FF, 0x1E606EFF, 0x2D1B00FF };

	uint32_t palette[4] = { 0 };
};

