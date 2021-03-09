#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cassert>
#include <SDL2/SDL.h>
#include "Mmu.h"
#include "Cpu.h"
#include "Ppu.h"

int main(int argc, char* argv[])
{
	if (!argv[1])
	{
		std::cout << "No rom specified." << std::endl;
		std::cout << "Usage:  kgb.exe <rom_filename> <bootrom_filename>" << std::endl;

		return -1;

	}

	if (!argv[2])
	{
		std::cout << "No boot rom specified." << std::endl;
		std::cout << "Usage:  kgb.exe <rom_filename> <bootrom_filename>" << std::endl;

		return -1;

	}

	Mmu* mmu = new Mmu();

	Ppu* ppu = new Ppu(mmu);

	Cpu* cpu = new Cpu(mmu, ppu);

	std::ifstream inFile;
	inFile.open(argv[1], std::ios::in | std::ios::binary);
	inFile.seekg(0, std::ios::end);
	std::streampos fileSize = inFile.tellg();
	inFile.seekg(0, std::ios::beg);
	if (!inFile.read((char*)mmu->GetROM(), std::min((int)fileSize, 0x800000)))
	{
		std::cerr << "Error reading file." << std::endl;
		exit(-1);
	}
	inFile.close();

	inFile.open(argv[2], std::ios::in | std::ios::binary);
	inFile.seekg(0, std::ios::end);
	fileSize = inFile.tellg();
	inFile.seekg(0, std::ios::beg);
	if (!inFile.read((char*)mmu->GetDMGBootRom(), std::min((int)fileSize, 0x100)))
	{
		std::cerr << "Error reading file." << std::endl;
		exit(-1);
	}
	inFile.close();

	/*for (int y = 0; y < 128; y++)
	{
		for (int x = 0; x < 16; x++)
		{
			std::cout << " " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (unsigned)mmu->GetROM()[y * 16 + x];
		}
		std::cout << std::endl;
	}*/

	const uint32_t palette_gbp_gray[4] = { 0xFFCDDBE0, 0xFF949FA8, 0xFF666B70, 0xFF262B2B };
	const uint32_t palette_gbp_green[4] = { 0xFFB4F4DB, 0xFF96C3AB, 0xFF78927B, 0xFF5A624C };
	uint32_t* screen = new uint32_t[160 * 144];

	uint32_t palette[4];

	memcpy(palette, palette_gbp_gray, sizeof(uint32_t) * 4);

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		std::cout << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
		return -1;
	}

	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
	SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

	SDL_Window* window = SDL_CreateWindow("kgb", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 576, SDL_WINDOW_OPENGL);

	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

	SDL_SetRenderDrawColor(renderer, palette[0] & 0x000000FF, (palette[0] & 0x0000FF00) >> 8, (palette[0] & 0x00FF0000) >> 16, 0xFF);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 160, 144);
	SDL_RendererInfo renderinfo;
	SDL_GetRendererInfo(renderer, &renderinfo);

	SDL_Event e;

	bool userQuit = false;

	while (!cpu->GetStopped() && !userQuit)
	{
		cpu->Tick();

		//SDL events to close window
		while (SDL_PollEvent(&e))
		{
			if (e.type == SDL_QUIT)
				userQuit = true;
		}

		if (cpu->GetTotalCycles() > (456 * 154))
		{
			cpu->ResetTotalCycles();

			uint8_t* fb = ppu->GetFramebuffer();

			for (int y = 0; y < 144; y++)
			{
				for (int x = 0; x < 160; x++)
				{
					screen[y * 160 + x] = palette[fb[y * 160 + x]];
				}
			}

			SDL_UpdateTexture(texture, NULL, screen, 4 * 160);
			SDL_RenderCopy(renderer, texture, NULL, NULL);
			SDL_RenderPresent(renderer);

		}
	}

	delete[] screen;

	return 0;
}