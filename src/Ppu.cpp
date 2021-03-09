#include "Ppu.h"

Ppu::Ppu(Mmu* __mmu) : mmu(__mmu)
{

}

void Ppu::Tick(uint16_t cycles)
{
	uint8_t lcdc = mmu->ReadByte(0xFF40);
	uint8_t stat = mmu->ReadByte(0xFF41);

	if (!(lcdc & LCD_ENABLE)) //todo: not sure this is right
		return;

	PpuCycles += cycles;

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
	uint8_t stat = mmu->ReadByte(0xFF41);

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
		RenderLine(); //render entire line at once upon entering hblank

		//trigger stat lcd interrupt for hblank
		if ((stat & STAT_HBLANK_ENABLE) && statIntAvail)
		{
			statIntAvail = false;
			uint8_t reg_if = mmu->ReadByte(0xFF0F);
			reg_if |= 0x02;
			mmu->WriteByte(0xFF0F, reg_if);
		}
		break;
	}
	case(1): //enter vblank
	{
		//copy working buffer to the public buffer upon entering vblank
		RenderFrame();

		if ((stat & STAT_VBLANK_ENABLE) && statIntAvail) //if enabled, request stat interrupt due to vblank
		{
			statIntAvail = false;
			uint8_t reg_if = mmu->ReadByte(0xFF0F);
			reg_if |= 0x02;
			mmu->WriteByte(0xFF0F, reg_if);
		}

		uint8_t reg_if = mmu->ReadByte(0xFF0F); // request v-blank interrupt
		reg_if |= 0x01;
		mmu->WriteByte(0xFF0F, reg_if);

		break;
	}
	case(2): //enter oam search
	{
		if ((stat & STAT_OAM_ENABLE) && statIntAvail)
		{
			statIntAvail = false;
			uint8_t reg_if = mmu->ReadByte(0xFF0F);
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
	uint8_t ly = currentLine;
	mmu->WriteByteDirect(0xFF44, ly);
}

void Ppu::CheckLYC()
{
	uint8_t stat = mmu->ReadByte(0xFF41);
	uint8_t ly = mmu->ReadByte(0xFF44);
	uint8_t lyc = mmu->ReadByte(0xFF45);

	stat &= 0xFB; //clear the LYC coincidence flag bit

	if (ly == lyc)
	{
		stat |= 0x04; //set LYC coincidence flag

		if ((stat & STAT_LYC_ENABLE) && statIntAvail)
		{
			statIntAvail = false;
			uint8_t reg_if = mmu->ReadByte(0xFF0F);
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

void Ppu::RenderLine()
{
	uint8_t lcdc = mmu->ReadByte(0xFF40);
	uint8_t scy = mmu->ReadByte(0xFF42);
	uint8_t scx = mmu->ReadByte(0xFF43);
	uint8_t bgp[4];

	bgp[0] = mmu->ReadByte(0xFF47) & 0x03;
	bgp[1] = (mmu->ReadByte(0xFF47) >> 2) & 0x03;
	bgp[2] = (mmu->ReadByte(0xFF47) >> 4) & 0x03;
	bgp[3] = (mmu->ReadByte(0xFF47) >> 6) & 0x03;

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
				u_bgDataIndex = mmu->ReadByte(bgMapBaseAddr + (bgMapY * 32) + bgMapX);

				tileDataL = mmu->ReadByte(bgDataBaseAddr + (u_bgDataIndex * 16) + (tileY * 2));
				tileDataH = mmu->ReadByte(bgDataBaseAddr + (u_bgDataIndex * 16) + (tileY * 2) + 1);
			}
			else
			{
				s_bgDataIndex = mmu->ReadByte(bgMapBaseAddr + (bgMapY * 32) + bgMapX);

				tileDataL = mmu->ReadByte(bgDataBaseAddr + (s_bgDataIndex * 16) + (tileY * 2));
				tileDataH = mmu->ReadByte(bgDataBaseAddr + (s_bgDataIndex * 16) + (tileY * 2) + 1);
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


	//render sprites

}

void Ppu::RenderFrame()
{
	//for (int y = 0; y < 144; y++)
	//{
	//	for(int x = 0; x < 160; x++)
	//		std::cout << (int)WorkingFrameBuffer[y * 160 + x];

	//	std::cout << std::endl;
	//}
	//std::cout << '\n' << '\n' << std::endl;
	memcpy(FrameBuffer, WorkingFrameBuffer, sizeof(uint8_t) * 160 * 144);
}