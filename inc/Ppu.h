#pragma once
#include <iostream>
#include "Mmu.h"
class Ppu
{
public:
	Ppu(Mmu* __mmu);
	void Tick(uint16_t cycles);
	uint8_t* GetFramebuffer();
private:
	Mmu* mmu;
	uint64_t PpuCycles{ 0 };
	uint8_t currentMode{ 2 };
	uint8_t currentLine{ 0 };
	uint8_t windowCounter{ 0 };

	const uint16_t OAM_CYCLES{ 80 };
	uint16_t DRAW_CYCLES{ 172 };
	
	//lcdc control bit names
	const uint8_t BG_ENABLE{ 0x01 };
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

	void RenderFrame();

	uint8_t FrameBuffer[160 * 144] = { 0 };
	uint8_t WorkingFrameBuffer[160 * 144] = { 0 };
};

