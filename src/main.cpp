#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <algorithm>
#include <SDL2/SDL.h>
#include "Mmu.h"
#include "Cpu.h"
#include "Ppu.h"
#include "Apu.h"
#include "Stopwatch.h"

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

	const uint32_t palette_gbp_gray[4] = { 0xFFCDDBE0, 0xFF949FA8, 0xFF666B70, 0xFF262B2B };
	const uint32_t palette_gbp_green[4] = { 0xFFB4F4DB, 0xFF96C3AB, 0xFF78927B, 0xFF5A624C };
	const uint32_t palette_platinum[4] = { 0xFFE8F0E0, 0xFFB0C0A8, 0xFF687850, 0xFF303018 };
	const uint32_t palette_luxa[4] = { 0xFFFFE6E6, 0xFFE6BEBE, 0xFF6E5050, 0xFF3C1E1E };
	const uint32_t palette_bgb[4] = { 0xFFD0F8E0, 0xFF70C088, 0xFF566834, 0xFF201808 };
	const uint32_t palette_mist[4] = { 0xFFC2F0C4, 0xFFA8B95A, 0xFF6E601E, 0xFF001B2D };
	
	uint32_t* screen = new uint32_t[160 * 144];

	uint32_t palette[4];

	memcpy(palette, palette_gbp_gray, sizeof(uint32_t) * 4);

	bool enabledGamepad = true;
	SDL_GameController* controller = NULL;
	
	bool enabledAudio = true;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		std::cout << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
		return -1;
	}
	if (SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		std::cout << "SDL could not initialize audio! SDL_Error: " << SDL_GetError() << std::endl;
		enabledAudio = false;
	}
	if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0)
	{
		std::cout << "SDL could not initialize gamepad subsystem. SDL_Error: " << SDL_GetError() << std::endl;
		enabledGamepad = false;
	}

	if (enabledGamepad)
	{
		for (int i = 0; i < SDL_NumJoysticks(); ++i) {
			if (SDL_IsGameController(i)) {
				controller = SDL_GameControllerOpen(i);
				if (controller) {
					break;
				}
				else {
					std::cout << "Could not open gamecontroller " << i << ". " << SDL_GetError() << std::endl;
				}
			}
		}
	}

	if (controller)
	{
		SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
	}


	SDL_Event e;

	bool userQuit = false;

	stopwatch::Stopwatch watch;
	stopwatch::Stopwatch title_timer;

	uint64_t frame_mus, average_frame_mus = 1;

	uint64_t running_frame_times[60] = { 0 };
	uint8_t frame_time_index = 0;

	

	Apu* apu = nullptr;

	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
	SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");

	SDL_Window* window;

	SDL_Renderer* renderer;

	if (enabledAudio)
	{
		apu = new Apu();
		SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
		window = SDL_CreateWindow("kgb", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 576, SDL_WINDOW_OPENGL);
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
		SDL_GL_SetSwapInterval(0);
	}
	else
	{
		SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
		window = SDL_CreateWindow("kgb", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 576, SDL_WINDOW_OPENGL);
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		SDL_GL_SetSwapInterval(1);
	}
	SDL_SetRenderDrawColor(renderer, palette[0] & 0x000000FF, (palette[0] & 0x0000FF00) >> 8, (palette[0] & 0x00FF0000) >> 16, 0xFF);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 160, 144);
	
	Mmu* mmu = new Mmu(apu);

	std::ifstream inFile;
	inFile.open(argv[1], std::ios::in | std::ios::binary);
	inFile.unsetf(std::ios::skipws);
	inFile.seekg(0, std::ios::end);
	int fileSize = inFile.tellg();
	inFile.seekg(0, std::ios::beg);

	fileSize = fileSize < 0x800000 ? fileSize : 0x800000;

	if (!inFile.read((char*)mmu->GetROM(), fileSize))
	{
		std::cerr << "Error reading file." << std::endl;
		exit(-1);
	}
	inFile.close();

	inFile.open(argv[2], std::ios::in | std::ios::binary);
	inFile.unsetf(std::ios::skipws);
	inFile.seekg(0, std::ios::end);
	fileSize = inFile.tellg();
	inFile.seekg(0, std::ios::beg);

	if (fileSize == 0x100)
	{
		mmu->SetCGBMode(false);
		if (!inFile.read((char*)mmu->GetDMGBootRom(), fileSize))
		{
			std::cerr << "Error reading file." << std::endl;
			exit(-1);
		}
	}
	else if (fileSize == 0x900)
	{
		mmu->SetCGBMode(true);
		if (!inFile.read((char*)mmu->GetCGBBootRom(), fileSize))
		{
			std::cerr << "Error reading file." << std::endl;
			exit(-1);
		}
	}
	else
	{
		std::cerr << "Invalid boot rom." << std::endl;
		exit(-1);
	}
	inFile.close();



	std::string romFileName = argv[1];

	mmu->ParseRomHeader(romFileName);
	
	Ppu* ppu = new Ppu(mmu);
	Cpu* cpu = new Cpu(mmu, ppu, apu);

	while (!userQuit)
	{
		if(apu == nullptr)
			cpu->Tick();

		if (cpu->GetFrameCycles() > (456 * 154) * mmu->DMASpeed)
		{
			cpu->SetFrameCycles(cpu->GetFrameCycles() - ((456 * 154) * mmu->DMASpeed));

			//if DMG use palette
			if (mmu->GetCGBMode())
			{
				std::memcpy(screen, ppu->GetColorFrameBuffer(), 160 * 144 * sizeof(uint32_t));
			}
			else
			{
				uint8_t* fb = ppu->GetFramebuffer();
				for (int y = 0; y < 144; y++)
				{
					for (int x = 0; x < 160; x++)
					{
						screen[y * 160 + x] = palette[fb[y * 160 + x]];
					}
				}
			}


			SDL_UpdateTexture(texture, NULL, screen, 4 * 160);
			SDL_RenderCopy(renderer, texture, NULL, NULL);
			SDL_RenderPresent(renderer);

			frame_mus = watch.elapsed<stopwatch::mus>();
			running_frame_times[frame_time_index] = frame_mus;
			frame_time_index = (frame_time_index + 1) % 60;
			if (title_timer.elapsed<stopwatch::ms>() > 500)
			{
				average_frame_mus = 0;
				for (int i = 0; i < 60; i++)
					average_frame_mus += running_frame_times[i];
				average_frame_mus = average_frame_mus / 60;
				title_timer.start();
			}

			
			uint16_t fps = std::floor(1000000.0 / (double)average_frame_mus);
			std::string title("KGB    FPS: ");
			title += std::to_string(fps);
			title += " uS: ";
			title += std::to_string(average_frame_mus);
			SDL_SetWindowTitle(window, title.c_str());

			watch.start();

			//SDL events to close window
			while (SDL_PollEvent(&e))
			{
				switch (e.type)
				{
				case(SDL_QUIT):
				{
					userQuit = true;
					break;
				}
				case(SDL_KEYDOWN):
				{
					switch (e.key.keysym.scancode)
					{
					case SDL_SCANCODE_W:
					case SDL_SCANCODE_UP:
						//press up;
						mmu->Joypad.directions &= ~(mmu->Joypad.up);
						break;

					case SDL_SCANCODE_S:
					case SDL_SCANCODE_DOWN:
						//press down
						mmu->Joypad.directions &= ~(mmu->Joypad.down);
						break;

					case SDL_SCANCODE_A:
					case SDL_SCANCODE_LEFT:
						//press left
						mmu->Joypad.directions &= ~(mmu->Joypad.left);
						break;

					case SDL_SCANCODE_D:
					case SDL_SCANCODE_RIGHT:
						//press right
						mmu->Joypad.directions &= ~(mmu->Joypad.right);
						break;

					case SDL_SCANCODE_Z:
					case SDL_SCANCODE_N:
						//press B
						mmu->Joypad.buttons &= ~(mmu->Joypad.b_button);
						break;

					case SDL_SCANCODE_X:
					case SDL_SCANCODE_M:
						//press A
						mmu->Joypad.buttons &= ~(mmu->Joypad.a_button);
						break;

					case SDL_SCANCODE_RSHIFT:
					case SDL_SCANCODE_LSHIFT:
						//press select
						mmu->Joypad.buttons &= ~(mmu->Joypad.select_button);
						break;

					case SDL_SCANCODE_RETURN:
					case SDL_SCANCODE_LCTRL:
					case SDL_SCANCODE_RCTRL:
						//press start
						mmu->Joypad.buttons &= ~(mmu->Joypad.start_button);
						break;

					default:
						break;
					}
					break;
				}
				case(SDL_KEYUP):
				{
					switch (e.key.keysym.scancode)
					{
					case SDL_SCANCODE_W:
					case SDL_SCANCODE_UP:
						//release up;
						mmu->Joypad.directions |= mmu->Joypad.up;
						break;

					case SDL_SCANCODE_S:
					case SDL_SCANCODE_DOWN:
						//release down
						mmu->Joypad.directions |= mmu->Joypad.down;
						break;

					case SDL_SCANCODE_A:
					case SDL_SCANCODE_LEFT:
						//release left
						mmu->Joypad.directions |= mmu->Joypad.left;
						break;

					case SDL_SCANCODE_D:
					case SDL_SCANCODE_RIGHT:
						//release right
						mmu->Joypad.directions |= mmu->Joypad.right;
						break;

					case SDL_SCANCODE_Z:
					case SDL_SCANCODE_N:
						//release B
						mmu->Joypad.buttons |= mmu->Joypad.b_button;
						break;

					case SDL_SCANCODE_X:
					case SDL_SCANCODE_M:
						//release A
						mmu->Joypad.buttons |= mmu->Joypad.a_button;
						break;

					case SDL_SCANCODE_RSHIFT:
					case SDL_SCANCODE_LSHIFT:
						//release select
						mmu->Joypad.buttons |= mmu->Joypad.select_button;
						break;

					case SDL_SCANCODE_RETURN:
					case SDL_SCANCODE_LCTRL:
					case SDL_SCANCODE_RCTRL:
						//release start
						mmu->Joypad.buttons |= mmu->Joypad.start_button;
						break;

					default:
						break;
					}
					break;
				}
				case(SDL_CONTROLLERBUTTONDOWN):
				{
					switch (e.cbutton.button)
					{
					case(SDL_CONTROLLER_BUTTON_A):
					{
						//press B
						mmu->Joypad.buttons &= ~(mmu->Joypad.b_button);
						break;
					}
					case(SDL_CONTROLLER_BUTTON_B):
					{
						//press A
						mmu->Joypad.buttons &= ~(mmu->Joypad.a_button);
						break;
					}
					case(SDL_CONTROLLER_BUTTON_START):
					{
						//press start
						mmu->Joypad.buttons &= ~(mmu->Joypad.start_button);
						break;
					}
					case(SDL_CONTROLLER_BUTTON_BACK):
					{
						//press select
						mmu->Joypad.buttons &= ~(mmu->Joypad.select_button);
						break;
					}
					case(SDL_CONTROLLER_BUTTON_DPAD_UP):
					{
						//press up;
						mmu->Joypad.directions &= ~(mmu->Joypad.up);
						break;
					}
					case(SDL_CONTROLLER_BUTTON_DPAD_DOWN):
					{
						//press down
						mmu->Joypad.directions &= ~(mmu->Joypad.down);
						break;
					}
					case(SDL_CONTROLLER_BUTTON_DPAD_LEFT):
					{
						//press left
						mmu->Joypad.directions &= ~(mmu->Joypad.left);
						break;
					}
					case(SDL_CONTROLLER_BUTTON_DPAD_RIGHT):
					{
						//press right
						mmu->Joypad.directions &= ~(mmu->Joypad.right);
						break;
					}
					default:
						break;
					}
					break;
				}
				case(SDL_CONTROLLERBUTTONUP):
				{
					switch (e.cbutton.button)
					{
					case(SDL_CONTROLLER_BUTTON_A):
					{
						//release B
						mmu->Joypad.buttons |= mmu->Joypad.b_button;
						break;
					}
					case(SDL_CONTROLLER_BUTTON_B):
					{
						//release A
						mmu->Joypad.buttons |= mmu->Joypad.a_button;
						break;
					}
					case(SDL_CONTROLLER_BUTTON_START):
					{
						//release start
						mmu->Joypad.buttons |= mmu->Joypad.start_button;
						break;
					}
					case(SDL_CONTROLLER_BUTTON_BACK):
					{
						//release select
						mmu->Joypad.buttons |= mmu->Joypad.select_button;
						break;
					}
					case(SDL_CONTROLLER_BUTTON_DPAD_UP):
					{
						//release up;
						mmu->Joypad.directions |= mmu->Joypad.up;
						break;
					}
					case(SDL_CONTROLLER_BUTTON_DPAD_DOWN):
					{
						//release down
						mmu->Joypad.directions |= mmu->Joypad.down;
						break;
					}
					case(SDL_CONTROLLER_BUTTON_DPAD_LEFT):
					{
						//release left
						mmu->Joypad.directions |= mmu->Joypad.left;
						break;
					}
					case(SDL_CONTROLLER_BUTTON_DPAD_RIGHT):
					{
						//release right
						mmu->Joypad.directions |= mmu->Joypad.right;
						break;
					}
					default:
						break;
					}
					break;
				}
				case(SDL_CONTROLLERAXISMOTION):
				{
					switch (e.caxis.axis)
					{
					case(SDL_CONTROLLER_AXIS_LEFTX):
					{
						if (e.caxis.value < -8000)
						{
							//press left
							mmu->Joypad.directions &= ~(mmu->Joypad.left);
							//release right
							mmu->Joypad.directions |= mmu->Joypad.right;
						}
						else if (e.caxis.value > 8000)
						{
							//press right
							mmu->Joypad.directions &= ~(mmu->Joypad.right);
							//release left
							mmu->Joypad.directions |= mmu->Joypad.left;
						}
						else
						{
							//release right
							mmu->Joypad.directions |= mmu->Joypad.right;
							//release left
							mmu->Joypad.directions |= mmu->Joypad.left;
						}
						break;
					}
					case(SDL_CONTROLLER_AXIS_LEFTY):
					{
						if (e.caxis.value < -8000)
						{
							//press up
							mmu->Joypad.directions &= ~(mmu->Joypad.up);
							//release down
							mmu->Joypad.directions |= mmu->Joypad.down;
						}
						else if (e.caxis.value > 8000)
						{
							//press down
							mmu->Joypad.directions &= ~(mmu->Joypad.down);
							//release up
							mmu->Joypad.directions |= mmu->Joypad.up;
						}
						else
						{
							//release up
							mmu->Joypad.directions |= mmu->Joypad.up;
							//release down
							mmu->Joypad.directions |= mmu->Joypad.down;
						}
						break;
					}
					default:
						break;
					}
					break;
				}
				case(SDL_CONTROLLERDEVICEREMOVED):
				{
					for (int i = 0; i < SDL_NumJoysticks(); ++i) {
						if (SDL_IsGameController(i)) {
							controller = SDL_GameControllerOpen(i);
							if (controller) {
								break;
							}
							else {
								std::cout << "Could not open gamecontroller " << i << ". " << SDL_GetError() << std::endl;
							}
						}
					}
					break;
				}
				case(SDL_CONTROLLERDEVICEADDED):
				{
					for (int i = 0; i < SDL_NumJoysticks(); ++i) {
						if (SDL_IsGameController(i)) {
							controller = SDL_GameControllerOpen(i);
							if (controller) {
								break;
							}
							else {
								std::cout << "Could not open gamecontroller " << i << ". " << SDL_GetError() << std::endl;
							}
						}
					}
					break;
				}
				default:
					break;
				}
			}
		}
	}

	mmu->SaveGame(romFileName);

	return 0;
}