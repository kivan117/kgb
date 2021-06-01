#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <cassert>
#include <algorithm>
#include <SDL2/SDL.h>
#include "Mmu.h"
#include "Cpu.h"
#include "Ppu.h"
#include "Apu.h"
#include "Serial.h"
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

	bool useLinkCable = false;
	bool isServer = false;
	std::string remoteAddr = "localhost";

	if (argv[3])
	{
		if (strcmp(argv[3], "server") == 0)
		{
			useLinkCable = true;
			isServer = true;
		}
		else if (strcmp(argv[3], "client") == 0)
		{
			useLinkCable = true;
			if (argv[4])
				remoteAddr = argv[4];
		}
	}

	bool enabledGamepad = true;
	bool enableControllerHaptic = false;
	SDL_GameController* controller = NULL;
	SDL_Haptic* controllerHaptic = NULL;
	
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

		//try rumble
		
		if (SDL_Init(SDL_INIT_HAPTIC) < 0)
			std::cout << "SDL could not initialize gamepad rumble subsystem. SDL_Error: " << SDL_GetError() << std::endl;
		else
			enableControllerHaptic = true;
	}

	if (enableControllerHaptic)
	{
		controllerHaptic = SDL_HapticOpenFromJoystick(SDL_GameControllerGetJoystick(controller));
		if (!controllerHaptic)
		{
			std::cout << "SDL could not initialize gamepad rumble. Controller may not support rumble. " << SDL_GetError() << std::endl;
			enableControllerHaptic = false;
		}
		else
		{
			if (SDL_HapticRumbleInit(controllerHaptic) < 0)
			{
				std::cout << "SDL could not initialize gamepad rumble. Controller may not support rumble. " << SDL_GetError() << std::endl;
				enableControllerHaptic = false;
			}
		}
	}

	SDL_Event e;

	bool userQuit = false;

	Apu* apu = nullptr;

	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
	SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");

	SDL_Window* window;

	SDL_Renderer* renderer;

	if (enabledAudio)
	{
		apu = new Apu();
		SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
		window = SDL_CreateWindow("kgb", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 576, SDL_WINDOW_SHOWN);
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

	SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 160, 144);
	
	Serial* linkCable{ nullptr };

	if (useLinkCable)
	{
		SDLNet_Init();

		linkCable = new Serial(isServer, remoteAddr, 11780);
	}		

	Mmu* mmu = new Mmu(apu, linkCable);

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
	
	Ppu* ppu = new Ppu(mmu, texture, renderer);
	Cpu* cpu = new Cpu(mmu, ppu, apu);

	while (!userQuit)
	{
		if (linkCable)
			linkCable->Tick();

		if (apu == nullptr)
		{
			//run one frame
			do
			{
				cpu->Tick();
			} while (cpu->GetFrameCycles() < (456 * 154) * mmu->DMASpeed);
		}
		else
		{
			static double carry_time = 0;
			int samples_ready = SDL_AudioStreamAvailable(apu->audio_stream);
			if (samples_ready < cpu->audio_frames_requested)
			{
				int cpu_ticks = cpu->audio_frames_requested - samples_ready; //audio frames requested
				double accurate_ticks;
				if (cpu->GetDoubleSpeedMode())
					accurate_ticks = (double)cpu_ticks * cpu->GetThrottle() * ((double)0x800000 / (double)48000) + carry_time;
				else
					accurate_ticks = (double)cpu_ticks * cpu->GetThrottle() * ((double)0x400000 / (double)48000) + carry_time;


				uint64_t pre_update_cpu_cycles = cpu->GetTotalCycles();

				while ((cpu->GetTotalCycles() - pre_update_cpu_cycles) < accurate_ticks) //tick emulator forward and fill audio buffer
				{
					uint64_t tick_cycles = cpu->GetTotalCycles();
					cpu->Tick();
					tick_cycles = cpu->GetTotalCycles() - tick_cycles;

					cpu->apu->Update(tick_cycles, cpu->GetDoubleSpeedMode());
				}

				carry_time = (cpu->GetTotalCycles() - pre_update_cpu_cycles) - accurate_ticks;
				cpu->frame_mus = cpu->watch.elapsed<stopwatch::mus>();
				cpu->running_frame_times[cpu->frame_time_index] = cpu->frame_mus;
				cpu->frame_time_index = (cpu->frame_time_index + 1) % 60;
				if (cpu->title_timer.elapsed<stopwatch::ms>() > 200)
				{
					cpu->average_frame_mus = 0;
					for (int i = 0; i < 60; i++)
						cpu->average_frame_mus += cpu->running_frame_times[i];
					cpu->average_frame_mus = cpu->average_frame_mus / 60;
					cpu->title_timer.start();

					double fps = ((double)(cpu->GetTotalCycles() - pre_update_cpu_cycles) * 1000000.0) / (((double)cpu->average_frame_mus) * 70224.0);
					if (cpu->GetDoubleSpeedMode())
						fps *= 0.5;
					cpu->titlestream.str(std::string());
					cpu->titlestream.precision(4);// << std::setprecision(4);
					cpu->titlestream << "KGB    FPS: ";
					cpu->titlestream << fps;
				}
				cpu->watch.start();


			}
		}
		
		if (cpu->GetFrameCycles() > (456 * 154) * mmu->DMASpeed)
		{
			cpu->SetFrameCycles(cpu->GetFrameCycles() - ((456 * 154) * mmu->DMASpeed));

			SDL_SetWindowTitle(window, cpu->titlestream.str().c_str());

			if (enableControllerHaptic)
			{
				if (mmu->rumbleStrength)
				{
					//Play rumble at 75% strenght for 500 milliseconds
					if (SDL_HapticRumblePlay(controllerHaptic, (double)mmu->rumbleStrength / (double)((456 * 154) * mmu->DMASpeed), 250) != 0)
					{
						printf("Warning: Unable to play rumble! %s\n", SDL_GetError());
					}
					mmu->rumbleStrength = 0;
				}
				//else
				//{
				//	SDL_HapticStopAll(controllerHaptic);
				//}
			}

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
					case(SDL_CONTROLLER_BUTTON_LEFTSHOULDER):
					{
						apu->ToggleMute();
						break;
					}
					case(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER):
					{
						double currentThrottle = cpu->GetThrottle();
						currentThrottle *= 2.0;
						if (currentThrottle > 4.0)
							currentThrottle = 1.0;
						cpu->SetThrottle(currentThrottle);
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
	if (enableControllerHaptic)
	{
		SDL_HapticStopAll(controllerHaptic);
		SDL_HapticClose(controllerHaptic);
	}
	return 0;
}