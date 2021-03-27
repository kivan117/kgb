#include "Ppu.h"
#include <algorithm>

Ppu::Ppu(Mmu* __mmu) : mmu(__mmu)
{

}

void Ppu::Tick(uint16_t cycles)
{
	uint8_t lcdc = mmu->ReadByteDirect(0xFF40);
	uint8_t stat = mmu->ReadByteDirect(0xFF41);

	if (!(lcdc & LCD_ENABLE)) //todo: not sure this is right
	{
		if(currentMode)
			SetMode(0);
		return;
	}

	PpuCycles += cycles / mmu->DMASpeed;
	PpuTotalCycles += cycles / mmu->DMASpeed;

	switch (currentMode)
	{
	case(0): //hblank
	{
		if (PpuCycles >= 456)
		{
			PpuCycles -= 456;

			SetLine((currentLine + 1) % 154);

			if (currentLine == 144) //start vblank
			{
				SetMode(1);
			}
			else
			{
				SetMode(2);
			}
		}
		break;
	}
	case(1): //vblank
	{
		//if (currentLine == 153 && PpuCycles >= 4) //some weird bug. not implementing for now.
		//	currentLine = 0;

		if (PpuCycles >= 456)
		{
			PpuCycles -= 456;
			SetLine((currentLine + 1) % 154);

			if (currentLine == 0)
			{
				SetMode(2);
			}
		}
		break;
	}
	case(2): //oam search
	{
		if (PpuCycles > OAM_CYCLES)
		{
			SetMode(3);
		}
		break;
	}
	case(3): //draw
	{
		//probably won't draw here actually. might draw at the end of each line instead.
		if (PpuCycles > (OAM_CYCLES + DRAW_CYCLES))
		{
			SetMode(0);
		}
		break;
	}
	default:
		std::cout << "PPU Mode undefined?! Mode: " << currentMode << std::endl;
		SetMode(0);
		break;
	}

	CheckLYC();

	return;
}

void Ppu::SetMode(uint8_t mode)
{
	uint8_t stat = mmu->ReadByteDirect(0xFF41);

	if (mode > 3)
	{
		std::cout << "WARNING: Request to set PPU to illegal mode! Requested Mode: " << mode << std::endl;
		std::cout << "Setting mode 0, HBLANK instead." << mode << std::endl;
		currentMode = 0;
	}
	else
		currentMode = mode;

	//update the mode for stat register
	stat &= ~0x03; //clear bottom 2 bits (the mode bits)
	stat |= currentMode;
	mmu->WriteByteDirect(0xFF41, stat);
	mmu->currentPPUMode = currentMode;

	switch (currentMode)
	{
	case(0): //enter hblank
	{
		if(currentLine < 144)
			RenderLine(); //render entire line at once upon entering hblank

		//trigger stat lcd interrupt for hblank
		if ((stat & STAT_HBLANK_ENABLE) && statIntAvail)
		{
			statIntAvail = false;
			uint8_t reg_if = mmu->ReadByteDirect(0xFF0F);
			reg_if |= 0x02;
			mmu->WriteByte(0xFF0F, reg_if);
		}

		if (mmu->isHDMAInProgress())
			mmu->DoHDMATransfer();

		break;
	}
	case(1): //enter vblank
	{
		//copy working buffer to the public buffer upon entering vblank
		RenderFrame();

		if ((stat & STAT_VBLANK_ENABLE) && statIntAvail) //if enabled, request stat interrupt due to vblank
		{
			statIntAvail = false;
			uint8_t reg_if = mmu->ReadByteDirect(0xFF0F);
			reg_if |= 0x02;
			mmu->WriteByte(0xFF0F, reg_if);
		}

		uint8_t reg_if = mmu->ReadByteDirect(0xFF0F); // request v-blank interrupt
		reg_if |= 0x01;
		mmu->WriteByte(0xFF0F, reg_if);

		windowCounter = 0;

		break;
	}
	case(2): //enter oam search
	{
		SpriteSearch();

		if ((stat & STAT_OAM_ENABLE) && statIntAvail)
		{
			statIntAvail = false;
			uint8_t reg_if = mmu->ReadByteDirect(0xFF0F);
			reg_if |= 0x02;
			mmu->WriteByte(0xFF0F, reg_if);
		}

		break;
	}
	case(3): //enter draw mode
	{
		break;
	}
	default:
		break;
	}

}

void Ppu::SetLine(uint8_t line)
{
	statIntAvail = true;

	if (line > 153)
	{
		std::cout << "WARNING: attempt to set PPU line beyond 153. Setting to 0 instead." << std::endl;
		currentLine = 0;
	}
	else
		currentLine = line;

	//update LY
	mmu->WriteByteDirect(0xFF44, currentLine);
}

void Ppu::CheckLYC()
{
	uint8_t stat = mmu->ReadByteDirect(0xFF41);
	uint8_t ly = mmu->ReadByteDirect(0xFF44);
	uint8_t lyc = mmu->ReadByteDirect(0xFF45);

	stat &= 0xFB; //clear the LYC coincidence flag bit

	if (ly == lyc)
	{
		stat |= 0x04; //set LYC coincidence flag

		if ((stat & STAT_LYC_ENABLE) && statIntAvail)
		{
			statIntAvail = false;
			uint8_t reg_if = mmu->ReadByteDirect(0xFF0F);
			reg_if |= 0x02;
			mmu->WriteByte(0xFF0F, reg_if);
		}

	}

	mmu->WriteByteDirect(0xFF41, stat);

}

uint8_t* Ppu::GetFramebuffer()
{
	return FrameBuffer;
}

uint32_t* Ppu::GetColorFrameBuffer()
{
	return ColorFrameBuffer;
}

void Ppu::RenderLine()
{
	if (mmu->GetCGBMode())
		RenderLineCGB();
	else
		RenderLineDMG();
}

void Ppu::RenderLineCGB()
{
	uint8_t lcdc = mmu->ReadByteDirect(0xFF40);
	uint8_t scy = mmu->ReadByteDirect(0xFF42);
	uint8_t scx = mmu->ReadByteDirect(0xFF43);

	//render background
	if (lcdc)
	{
		uint16_t bgMapBaseAddr = (lcdc & 0x08) ? 0x9C00 : 0x9800;
		uint16_t bgDataBaseAddr = (lcdc & 0x10) ? 0x8000 : 0x9000;
		uint8_t u_bgDataIndex = 0;
		int8_t s_bgDataIndex = 0;

		uint8_t bgMapY = ((scy + currentLine) % 256) >> 3; // Background Map Tile Y
		uint8_t tileY = ((scy + currentLine) % 256) & 0x07; // the number of lines from the top of the tile

		for (int screenX = 0; screenX < 160; screenX++)
		{
			uint8_t bgMapX = ((scx + screenX) % 256) >> 3; // Background Map Tile X
			uint8_t tileX = ((scx + screenX) % 256) & 0x07; // number of pixels from left to right on the tile
			uint8_t tileDataH, tileDataL;
			uint8_t bgColorIndex = 0;
			uint8_t bgPaletteNum = 0;
			uint8_t bgTileVRAMBank = 0;

			bool bgXFlip = 0;
			bool bgYFlip = 0;
			bool bgTilePriorityBit = 0;

			if (bgDataBaseAddr == 0x8000)
			{
				u_bgDataIndex = mmu->ReadVRAMDirect(bgMapBaseAddr + (bgMapY * 32) + bgMapX, 0);
				uint8_t bgAttrMapData = mmu->ReadVRAMDirect(bgMapBaseAddr + (bgMapY * 32) + bgMapX, 1);
				bgPaletteNum = bgAttrMapData & 0x07;
				bgTileVRAMBank = (bgAttrMapData & 0x08) >> 3;
				
				bgXFlip = (bgAttrMapData & 0x20) >> 5;
				bgYFlip = (bgAttrMapData & 0x40) >> 6;
				bgTilePriorityBit = (bgAttrMapData & 0x80) >> 7;

				if (bgYFlip)
				{
					tileDataL = mmu->ReadVRAMDirect(bgDataBaseAddr + (u_bgDataIndex * 16) + ((7 - tileY) * 2), bgTileVRAMBank);
					tileDataH = mmu->ReadVRAMDirect(bgDataBaseAddr + (u_bgDataIndex * 16) + ((7 - tileY) * 2) + 1, bgTileVRAMBank);
				}
				else
				{
					tileDataL = mmu->ReadVRAMDirect(bgDataBaseAddr + (u_bgDataIndex * 16) + (tileY * 2), bgTileVRAMBank);
					tileDataH = mmu->ReadVRAMDirect(bgDataBaseAddr + (u_bgDataIndex * 16) + (tileY * 2) + 1, bgTileVRAMBank);
				}
			}
			else
			{
				s_bgDataIndex = mmu->ReadVRAMDirect(bgMapBaseAddr + (bgMapY * 32) + bgMapX, 0);
				uint8_t bgAttrMapData = mmu->ReadVRAMDirect(bgMapBaseAddr + (bgMapY * 32) + bgMapX, 1);
				bgPaletteNum = bgAttrMapData & 0x07;
				bgTileVRAMBank = (bgAttrMapData & 0x08) >> 3;

				bgXFlip = (bgAttrMapData & 0x20) >> 5;
				bgYFlip = (bgAttrMapData & 0x40) >> 6;
				bgTilePriorityBit = (bgAttrMapData & 0x80) >> 7;

				if (bgYFlip)
				{
					tileDataL = mmu->ReadVRAMDirect(bgDataBaseAddr + (s_bgDataIndex * 16) + ((7 - tileY) * 2), bgTileVRAMBank);
					tileDataH = mmu->ReadVRAMDirect(bgDataBaseAddr + (s_bgDataIndex * 16) + ((7 - tileY) * 2) + 1, bgTileVRAMBank);
				}
				else
				{
					tileDataL = mmu->ReadVRAMDirect(bgDataBaseAddr + (s_bgDataIndex * 16) + (tileY * 2), bgTileVRAMBank);
					tileDataH = mmu->ReadVRAMDirect(bgDataBaseAddr + (s_bgDataIndex * 16) + (tileY * 2) + 1, bgTileVRAMBank);
				}
			}

			if (bgXFlip)
				tileX = 7 - tileX;

			uint8_t colorIndex = (((tileDataH >> (7 - tileX) & 0x01) << 1) | (tileDataL >> (7 - tileX) & 0x01));
			WorkingFrameBuffer[currentLine * 160 + screenX] = colorIndex;
			WorkingColorFrameBuffer[currentLine * 160 + screenX] = mmu->GetBGPColor(bgPaletteNum, colorIndex);
			bgPixelPriority[screenX] = bgTilePriorityBit && colorIndex;
		}
	}
	else
	{
		for (int screenX = 0; screenX < 160; screenX++)
		{
			WorkingColorFrameBuffer[currentLine * 160 + screenX] = 0xFFFFFFFF;
		}
	}

	//render window
	uint8_t wY = mmu->ReadByteDirect(0xFF4A);
	uint8_t wX = mmu->ReadByteDirect(0xFF4B);

	if ((lcdc & WINDOW_ENABLE) && (wY <= currentLine))
	{
		uint8_t windowDrawn = 0;
		uint16_t winMapBaseAddr = (lcdc & 0x40) ? 0x9C00 : 0x9800;
		uint16_t winDataBaseAddr = (lcdc & 0x10) ? 0x8000 : 0x9000;
		uint8_t u_winDataIndex = 0;
		int8_t s_winDataIndex = 0;

		//uint8_t winMapY = ((currentLine - wY) % 256) >> 3; // Window Map Tile Y
		//uint8_t tileY = ((currentLine - wY) % 256) & 0x07; // the number of lines from the top of the tile

		uint8_t winMapY = (windowCounter % 256) >> 3; // Window Map Tile Y
		uint8_t tileY = (windowCounter % 256) & 0x07; // the number of lines from the top of the tile

		for (int screenX = 0; screenX < 160; screenX++)
		{
			if (screenX + 7 < wX)
				continue;

			windowDrawn |= 1;

			uint8_t winMapX = (((screenX + 7) - wX) % 256) >> 3; // Window Map Tile X
			uint8_t tileX = (((screenX + 7) - wX) % 256) & 0x07; // number of pixels from left to right on the tile
			uint8_t tileDataH, tileDataL;
			uint8_t winColorIndex = 0;
			uint8_t winPaletteNum = 0;
			uint8_t winTileVRAMBank = 0;

			bool winXFlip = 0;
			bool winYFlip = 0;
			bool winTilePriorityBit = 0;

			if (winDataBaseAddr == 0x8000)
			{
				u_winDataIndex = mmu->ReadVRAMDirect(winMapBaseAddr + (winMapY * 32) + winMapX, 0);
				uint8_t winAttrMapData = mmu->ReadVRAMDirect(winMapBaseAddr + (winMapY * 32) + winMapX, 1);

				winPaletteNum = winAttrMapData & 0x07;
				winTileVRAMBank = (winAttrMapData & 0x08) >> 3;

				winXFlip = (winAttrMapData & 0x20) >> 5;
				winYFlip = (winAttrMapData & 0x40) >> 6;
				winTilePriorityBit = (winAttrMapData & 0x80) >> 7;

				if (winYFlip)
				{
					tileDataL = mmu->ReadVRAMDirect(winDataBaseAddr + (u_winDataIndex * 16) + ((7 - tileY) * 2), winTileVRAMBank);
					tileDataH = mmu->ReadVRAMDirect(winDataBaseAddr + (u_winDataIndex * 16) + ((7 - tileY) * 2) + 1, winTileVRAMBank);
				}
				else
				{
					tileDataL = mmu->ReadVRAMDirect(winDataBaseAddr + (u_winDataIndex * 16) + (tileY * 2), winTileVRAMBank);
					tileDataH = mmu->ReadVRAMDirect(winDataBaseAddr + (u_winDataIndex * 16) + (tileY * 2) + 1, winTileVRAMBank);
				}
			}
			else
			{
				s_winDataIndex = mmu->ReadVRAMDirect(winMapBaseAddr + (winMapY * 32) + winMapX, 0);
				uint8_t winAttrMapData = mmu->ReadVRAMDirect(winMapBaseAddr + (winMapY * 32) + winMapX, 1);

				winPaletteNum = winAttrMapData & 0x07;
				winTileVRAMBank = (winAttrMapData & 0x08) >> 3;

				winXFlip = (winAttrMapData & 0x20) >> 5;
				winYFlip = (winAttrMapData & 0x40) >> 6;
				winTilePriorityBit = (winAttrMapData & 0x80) >> 7;

				if (winYFlip)
				{
					tileDataL = mmu->ReadVRAMDirect(winDataBaseAddr + (s_winDataIndex * 16) + ((7 - tileY) * 2), winTileVRAMBank);
					tileDataH = mmu->ReadVRAMDirect(winDataBaseAddr + (s_winDataIndex * 16) + ((7 - tileY) * 2) + 1, winTileVRAMBank);
				}
				else
				{
					tileDataL = mmu->ReadVRAMDirect(winDataBaseAddr + (s_winDataIndex * 16) + (tileY * 2), winTileVRAMBank);
					tileDataH = mmu->ReadVRAMDirect(winDataBaseAddr + (s_winDataIndex * 16) + (tileY * 2) + 1, winTileVRAMBank);
				}
			}

			if (winXFlip)
				tileX = 7 - tileX;

			uint8_t colorIndex = (((tileDataH >> (7 - tileX) & 0x01) << 1) | (tileDataL >> (7 - tileX) & 0x01));
			WorkingFrameBuffer[currentLine * 160 + screenX] = colorIndex;
			WorkingColorFrameBuffer[currentLine * 160 + screenX] = mmu->GetBGPColor(winPaletteNum, colorIndex);
			bgPixelPriority[screenX] = winTilePriorityBit && colorIndex;
		}

		windowCounter += windowDrawn;
	}


	//render sprites

	if (lcdc & SPRITE_ENABLE)
	{
		uint8_t spriteHeight = 8 + (((lcdc & 0x04) << 1)); // 8 or 16 tall
		//uint8_t obp_zero[4];
		//uint8_t obp_one[4];

		//ignore the bottom 2 bits on the palettes since color index 0x00 is "transparent" for sprites
		//obp_zero[0] = 0x00;
		//obp_zero[1] = (mmu->ReadByteDirect(0xFF48) >> 2) & 0x03;
		//obp_zero[2] = (mmu->ReadByteDirect(0xFF48) >> 4) & 0x03;
		//obp_zero[3] = (mmu->ReadByteDirect(0xFF48) >> 6) & 0x03;
		//obp_one[0] = 0x00;
		//obp_one[1] = (mmu->ReadByteDirect(0xFF49) >> 2) & 0x03;
		//obp_one[2] = (mmu->ReadByteDirect(0xFF49) >> 4) & 0x03;
		//obp_one[3] = (mmu->ReadByteDirect(0xFF49) >> 6) & 0x03;

		for (int pixelX = 8; pixelX < 168; pixelX++)
		{
			//uint8_t minX = 0; //lowest x value that can be drawn over

			for (int i = 0; i < lineSpriteCount; i++)
			{
				if (lineSprites[i].x <= pixelX - 8 || lineSprites[i].x > pixelX)
					continue;

				uint8_t tileY = lineSprites[i].yflip ? ((spriteHeight - 1) - (currentLine - (lineSprites[i].y - 16))) & (spriteHeight - 1) : (currentLine - (lineSprites[i].y - 16)) & (spriteHeight - 1);
				
				uint8_t tileDataL = mmu->ReadVRAMDirect((0x8000 + (lineSprites[i].tile_id * 16) + (tileY * 2)), lineSprites[i].vram_bank);
				uint8_t tileDataH = mmu->ReadVRAMDirect((0x8000 + (lineSprites[i].tile_id * 16) + (tileY * 2) + 1), lineSprites[i].vram_bank);
				//uint8_t tileDataL = mmu->ReadByteDirect(0x8000 + (lineSprites[i].tile_id * 16) + (tileY * 2));
				//uint8_t tileDataH = mmu->ReadByteDirect(0x8000 + (lineSprites[i].tile_id * 16) + (tileY * 2) + 1);
				
				int16_t coordX = lineSprites[i].x - 8;
				int subX = (pixelX - 8) - coordX;
				uint8_t tileX = lineSprites[i].xflip ? 7 - subX : subX;
				
				uint8_t color = (((tileDataH >> (7 - tileX) & 0x01) << 1) | (tileDataL >> (7 - tileX) & 0x01));
				
				if (lcdc & CGB_BG_PRIORITY)
				{
					if (bgPixelPriority[coordX + subX])
						break;
					if (lineSprites[i].bg_priority && (WorkingFrameBuffer[currentLine * 160 + coordX + subX] != 0)) //don't draw over background, but do move minX
						break;
				}
				
				if (color != 0x00) //transparent
				{
					WorkingColorFrameBuffer[currentLine * 160 + coordX + subX] = mmu->GetOBPColor(lineSprites[i].palette, color);
					break;
				}
			}

		}

	}

}

void Ppu::RenderLineDMG()
{
	uint8_t lcdc = mmu->ReadByteDirect(0xFF40);
	uint8_t scy = mmu->ReadByteDirect(0xFF42);
	uint8_t scx = mmu->ReadByteDirect(0xFF43);
	uint8_t bgp[4] = { 0 };

	bgp[0] = mmu->ReadByteDirect(0xFF47) & 0x03;
	bgp[1] = (mmu->ReadByteDirect(0xFF47) >> 2) & 0x03;
	bgp[2] = (mmu->ReadByteDirect(0xFF47) >> 4) & 0x03;
	bgp[3] = (mmu->ReadByteDirect(0xFF47) >> 6) & 0x03;

	//render background
	if (lcdc & BG_ENABLE)
	{
		uint16_t bgMapBaseAddr = (lcdc & 0x08) ? 0x9C00 : 0x9800;
		uint16_t bgDataBaseAddr = (lcdc & 0x10) ? 0x8000 : 0x9000;
		uint8_t u_bgDataIndex = 0;
		int8_t s_bgDataIndex = 0;

		uint8_t bgMapY = ((scy + currentLine) % 256) >> 3; // Background Map Tile Y
		uint8_t tileY = ((scy + currentLine) % 256) & 0x07; // the number of lines from the top of the tile

		for (int screenX = 0; screenX < 160; screenX++)
		{
			uint8_t bgMapX = ((scx + screenX) % 256) >> 3; // Background Map Tile X
			uint8_t tileX = ((scx + screenX) % 256) & 0x07; // number of pixels from left to right on the tile
			uint8_t tileDataH, tileDataL;

			if (bgDataBaseAddr == 0x8000)
			{
				u_bgDataIndex = mmu->ReadByteDirect(bgMapBaseAddr + (bgMapY * 32) + bgMapX);

				tileDataL = mmu->ReadByteDirect(bgDataBaseAddr + (u_bgDataIndex * 16) + (tileY * 2));
				tileDataH = mmu->ReadByteDirect(bgDataBaseAddr + (u_bgDataIndex * 16) + (tileY * 2) + 1);
			}
			else
			{
				s_bgDataIndex = mmu->ReadByteDirect(bgMapBaseAddr + (bgMapY * 32) + bgMapX);

				tileDataL = mmu->ReadByteDirect(bgDataBaseAddr + (s_bgDataIndex * 16) + (tileY * 2));
				tileDataH = mmu->ReadByteDirect(bgDataBaseAddr + (s_bgDataIndex * 16) + (tileY * 2) + 1);
			}

			WorkingFrameBuffer[currentLine * 160 + screenX] = bgp[(((tileDataH >> (7 - tileX) & 0x01) << 1) | (tileDataL >> (7 - tileX) & 0x01))];
		}
	}
	else
	{
		for (int screenX = 0; screenX < 160; screenX++)
		{
			WorkingFrameBuffer[currentLine * 160 + screenX] = 0x00;
		}
	}

	//render window
	uint8_t wY = mmu->ReadByteDirect(0xFF4A);
	uint8_t wX = mmu->ReadByteDirect(0xFF4B);

	if ((lcdc & WINDOW_ENABLE) && (lcdc & BG_ENABLE) && (wY <= currentLine))
	{
		uint8_t windowDrawn = 0;
		uint16_t winMapBaseAddr = (lcdc & 0x40) ? 0x9C00 : 0x9800;
		uint16_t winDataBaseAddr = (lcdc & 0x10) ? 0x8000 : 0x9000;
		uint8_t u_winDataIndex = 0;
		int8_t s_winDataIndex = 0;

		//uint8_t winMapY = ((currentLine - wY) % 256) >> 3; // Window Map Tile Y
		//uint8_t tileY = ((currentLine - wY) % 256) & 0x07; // the number of lines from the top of the tile

		uint8_t winMapY = (windowCounter % 256) >> 3; // Window Map Tile Y
		uint8_t tileY = (windowCounter % 256) & 0x07; // the number of lines from the top of the tile

		for (int screenX = 0; screenX < 160; screenX++)
		{
			if (screenX + 7 < wX)
				continue;

			windowDrawn |= 1;

			uint8_t winMapX = (((screenX + 7) - wX) % 256) >> 3; // Window Map Tile X
			uint8_t tileX = (((screenX + 7) - wX) % 256) & 0x07; // number of pixels from left to right on the tile
			uint8_t tileDataH, tileDataL;

			if (winDataBaseAddr == 0x8000)
			{
				u_winDataIndex = mmu->ReadByteDirect(winMapBaseAddr + (winMapY * 32) + winMapX);

				tileDataL = mmu->ReadByteDirect(winDataBaseAddr + (u_winDataIndex * 16) + (tileY * 2));
				tileDataH = mmu->ReadByteDirect(winDataBaseAddr + (u_winDataIndex * 16) + (tileY * 2) + 1);
			}
			else
			{
				s_winDataIndex = mmu->ReadByteDirect(winMapBaseAddr + (winMapY * 32) + winMapX);

				tileDataL = mmu->ReadByteDirect(winDataBaseAddr + (s_winDataIndex * 16) + (tileY * 2));
				tileDataH = mmu->ReadByteDirect(winDataBaseAddr + (s_winDataIndex * 16) + (tileY * 2) + 1);
			}

			WorkingFrameBuffer[currentLine * 160 + screenX] = bgp[(((tileDataH >> (7 - tileX) & 0x01) << 1) | (tileDataL >> (7 - tileX) & 0x01))];
		}

		windowCounter += windowDrawn;
	}

	//render sprites

	if (lcdc & SPRITE_ENABLE)
	{
		uint8_t spriteHeight = 8 + (((lcdc & 0x04) << 1)); // 8 or 16 tall
		uint8_t obp_zero[4] = { 0 };
		uint8_t obp_one[4] = { 0 };

		//ignore the bottom 2 bits on the palettes since color index 0x00 is "transparent" for sprites
		obp_zero[0] = 0x00;
		obp_zero[1] = (mmu->ReadByteDirect(0xFF48) >> 2) & 0x03;
		obp_zero[2] = (mmu->ReadByteDirect(0xFF48) >> 4) & 0x03;
		obp_zero[3] = (mmu->ReadByteDirect(0xFF48) >> 6) & 0x03;
		obp_one[0] = 0x00;
		obp_one[1] = (mmu->ReadByteDirect(0xFF49) >> 2) & 0x03;
		obp_one[2] = (mmu->ReadByteDirect(0xFF49) >> 4) & 0x03;
		obp_one[3] = (mmu->ReadByteDirect(0xFF49) >> 6) & 0x03;

		for (int pixelX = 8; pixelX < 168; pixelX++)
		{
			//uint8_t minX = 0; //lowest x value that can be drawn over

			for (int i = 0; i < lineSpriteCount; i++)
			{
				if (lineSprites[i].x <= pixelX - 8 || lineSprites[i].x > pixelX)
					continue;

				uint8_t tileY = lineSprites[i].yflip ? ((spriteHeight - 1) - (currentLine - (lineSprites[i].y - 16))) & (spriteHeight - 1) : (currentLine - (lineSprites[i].y - 16)) & (spriteHeight - 1);
				uint8_t tileDataL = mmu->ReadByteDirect(0x8000 + (lineSprites[i].tile_id * 16) + (tileY * 2));
				uint8_t tileDataH = mmu->ReadByteDirect(0x8000 + (lineSprites[i].tile_id * 16) + (tileY * 2) + 1);
				int16_t coordX = lineSprites[i].x - 8;
				int subX = (pixelX - 8) - coordX;
				uint8_t tileX = lineSprites[i].xflip ? 7 - subX : subX;
				uint8_t color = (((tileDataH >> (7 - tileX) & 0x01) << 1) | (tileDataL >> (7 - tileX) & 0x01));
				if (lineSprites[i].bg_priority && (WorkingFrameBuffer[currentLine * 160 + coordX + subX] != 0)) //don't draw over background, but do move minX
				{
					break;
				}
				else if (color != 0x00) //transparent
				{
					if (lineSprites[i].palette)
						WorkingFrameBuffer[currentLine * 160 + coordX + subX] = obp_one[color];
					else
						WorkingFrameBuffer[currentLine * 160 + coordX + subX] = obp_zero[color];

					break;
				}
			}

		}		

	}

}

void Ppu::RenderFrame()
{
	if (mmu->GetCGBMode())
		memcpy(ColorFrameBuffer, WorkingColorFrameBuffer, sizeof(uint32_t) * 160 * 144);
	else
		memcpy(FrameBuffer, WorkingFrameBuffer, sizeof(uint8_t) * 160 * 144);
}

void Ppu::SpriteSearch()
{
	lineSpriteCount = 0;
	lineSprites.fill(Sprite());

	uint16_t OAMBaseAddr = 0xFE00;
	uint8_t lcdc = mmu->ReadByteDirect(0xFF40);

	if (!(lcdc & SPRITE_ENABLE))
		return;

	uint8_t spriteHeight = 8 + ((lcdc & 0x04) << 1);
	for (int i = 0; i < 40; i++)
	{
		uint8_t offset = i * 4;

		allSprites[i].y = mmu->ReadByteDirect(OAMBaseAddr + offset);
		allSprites[i].x = mmu->ReadByteDirect(OAMBaseAddr + offset + 1);
		allSprites[i].tile_id = mmu->ReadByteDirect(OAMBaseAddr + offset + 2);
		if (spriteHeight > 8) { allSprites[i].tile_id &= 0xFE; };
		uint8_t spriteAttribData = mmu->ReadByteDirect(OAMBaseAddr + offset + 3);
		allSprites[i].bg_priority = spriteAttribData & 0x80;
		allSprites[i].yflip = spriteAttribData & 0x40;
		allSprites[i].xflip = spriteAttribData & 0x20;
		if (mmu->GetCGBMode())
		{
			allSprites[i].vram_bank = (spriteAttribData & 0x08) >> 3;
			allSprites[i].palette = spriteAttribData & 0x07;
		}
		else
			allSprites[i].palette = (spriteAttribData & 0x10) >> 4;
	}
	for (int i = 0; i < 40 && lineSpriteCount < 10; i++)
	{
		if (currentLine + 16 >= allSprites[i].y && currentLine + 16 < allSprites[i].y + spriteHeight) //this sprite crosses the current draw line
		{
			lineSprites[lineSpriteCount] = allSprites[i];
			lineSprites[lineSpriteCount].index = lineSpriteCount;
			lineSpriteCount++;
		}
	}
	
	if(lineSpriteCount > 0 && !mmu->GetCGBMode())
		std::sort(lineSprites.begin(), lineSprites.end());

	return;
}