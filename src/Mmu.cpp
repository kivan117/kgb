#include "Mmu.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>

//0000 	3FFF 	16 KiB ROM bank 00 	From cartridge, usually a fixed bank
//4000 	7FFF 	16 KiB ROM Bank 01~NN 	From cartridge, switchable bank via mapper(if any)
//8000 	9FFF 	8 KiB Video RAM(VRAM) 	In CGB mode, switchable bank 0 / 1
//A000 	BFFF 	8 KiB External RAM 	From cartridge, switchable bank if any
//C000 	CFFF 	4 KiB Work RAM(WRAM)
//D000 	DFFF 	4 KiB Work RAM(WRAM) 	In CGB mode, switchable bank 1~7
//E000 	FDFF 	Mirror of C000~DDFF(ECHO RAM) 	Nintendo says use of this area is prohibited.
//FE00 	FE9F 	Sprite attribute table(OAM)
//FEA0 	FEFF 	Not Usable 	Nintendo says use of this area is prohibited
//FF00 	FF7F 	I / O Registers
//FF80 	FFFE 	High RAM(HRAM)
//FFFF 	FFFF 	Interrupts Enable Register(IE)

Mmu::Mmu()
{
	//initialize all the various io registers to their correct starting values

	Memory[0xFF00] = 0xFF; //stub initial input to all buttons released

	//std::fill(Memory, Memory + 0x10000, 0xFF);
}

void Mmu::Tick(uint16_t cycles)
{
	//todo: make rtc code not suck
	if (doesRTCExist && ((rtcRegValues[DH] & 0x40) == 0)) //rtc exists and halt bit not set
	{
		rtc_clock += cycles;
		while (rtc_clock > 128)
		{
			rtc_clock -= 128;
			rtc_ticks++;

			while (rtc_ticks >= 0x8000)
			{
				rtc_ticks -= 0x8000;
				rtcRegValues[S] += 1;
			}
			if (rtcRegValues[S] == 0x3C)
			{
				rtcRegValues[S] = 0x00;

				rtcRegValues[M] += 1;

				if (rtcRegValues[M] == 0x3C)
				{
					rtcRegValues[M] = 0x00;

					rtcRegValues[H] += 1;

					if (rtcRegValues[H] == 0x18)
					{
						rtcRegValues[H] = 0x00;

						if (rtcRegValues[DL] == 0xFF)
						{
							if (rtcRegValues[DH] & 0x01)
							{
								rtcRegValues[DH] |= 0x80; //set day counter overflow
								rtcRegValues[DH] &= 0xFE; //clear day counter high bit
							}
							else
								rtcRegValues[DH] |= 0x01;
						}

						rtcRegValues[DL] += 1;
					}
				}
				
			}
			rtcRegValues[S] &= 0x3F;
			rtcRegValues[M] &= 0x3F;
			rtcRegValues[H] &= 0x1F;
			rtcRegValues[DH] &= 0xC1;
			
		}
	}

	if (!DMAInProgress)
		return;

	uint16_t totalCycles = DMACycles + cycles;

	while (DMACycles < totalCycles && DMACycles <= 644)
	{
		DMACycles+=4;
		if (DMACycles < 8)
			continue;
		if (DMACycles % 4 == 0)
			Memory[0xFE00 + (DMACycles / 4) - 2] = ReadByteDirect(DMABaseAddr + (DMACycles / 4) - 2);
	}

	if (DMACycles >= 644)
	{
		DMACycles = 0;
		DMAInProgress = false;
	}
}

uint8_t Mmu::ReadByte(uint16_t addr)
{
	if (DMAInProgress && addr < 0xFF80) //DMA conflict on bus and not in HRAM
	{
		return ReadByteDirect(DMABaseAddr + (DMACycles / 4) - 2);
	}

	return ReadByteDirect(addr);
}

uint8_t Mmu::ReadByteDirect(uint16_t addr)
{
	if (addr < 0x100 && bootRomEnabled && (!cgbMode)) //DMG Boot Rom
		return DMGBootROM[addr];
	if (((addr < 0x100) || addr > 0x1FF && addr < 0x900 ) && bootRomEnabled && (cgbMode)) //CGB Boot Rom
		return CGBBootROM[addr];
	if (addr < 0x4000) //ROM, Bank 0
	{
		if (currentMBC == MBC1 && mbc1Mode == 0x01 && totalRomBanks > 0x20)
		{
			return ROM[0x4000 * ((hiBank << 5) % totalRomBanks) + (addr)];
		}
		//MBC3 and MBC5 always maps bank 00 here

		return ROM[addr];
	}
	if (addr < 0x8000) //ROM, bank N
	{
		return ROM[(0x4000 * (currentRomBank % totalRomBanks)) + (addr - 0x4000)];
	}
	if (addr < 0xA000) //VRAM
	{
		return VRAM[currentVRAMBank][addr & 0x1FFF];
	}
	if (addr > 0x9FFF && addr < 0xC000) //external cartridge ram (possibly banked)
	{
		if (doesRTCExist && isRTCEnabled && (mappedRTCReg < RTCREGS::NONE))
		{
			if (isRTCLatched)
			{
				return latchedRtcRegValues[mappedRTCReg];
			}
			else
			{
				return rtcRegValues[mappedRTCReg];
			}
		}
		if (!isCartRamEnabled)
			return 0xFF;
		switch (currentMBC)
		{
		case(MBC1):
		{
			if (totalRamBanks > 1 && mbc1Mode == 1)
				return ReadCartRam((uint32_t)((uint32_t)hiBank << 13) + (addr & 0x1FFF));
			else if (totalRamBanks)
				return ReadCartRam((addr & 0x1FFF));
			return 0xFF;
			break;
		}
		case(MBC2):
		{
			return ReadCartRam((addr & 0x01FF));
			break;
		}
		case(MBC3):
		{
			if (totalRamBanks > 1)
				return ReadCartRam((uint32_t)((uint32_t)hiBank << 13) + (addr & 0x1FFF));
			else if (totalRamBanks)
				return ReadCartRam((addr & 0x1FFF));
			return 0xFF;
			break;
		case(MBC5):
		{
			if (totalRamBanks > 1)
				return ReadCartRam((uint32_t)((uint32_t)currentRamBank << 13) + (addr & 0x1FFF));
			else if (totalRamBanks)
				return ReadCartRam((addr & 0x1FFF));
			return 0xFF;
			break;
		}
		}
		default:
			break;
		}
	}
	if (addr > 0xBFFF && addr < 0xD000) //WRAM bank 0
	{
		return WRAM[0][addr & 0x0FFF];
	}
	if (addr > 0xCFFF && addr < 0xE000) //WRAM high bank (cgb mode), WRAM bank 1 in DMG mode
	{
		return WRAM[currentWRAMBank][addr & 0x0FFF];
	}
	if (addr > 0xDFFF && addr < 0xFE00) //echo ram
		return Memory[addr - 0x2000];
	if (addr > 0xFE9F && addr < 0xFF00) // prohibited area. todo: during OAM, return FF and trigger sprite bug. else return 00
		return 0x00; 

	if (addr == 0xFF00) // joypad
	{
		uint8_t value = Memory[0xFF00];

		if (!(value & 0x20)) //buttons selected
		{
			value &= 0xF0 | Joypad.buttons;
		}
		else if (!(value & 0x10)) //directions selected
		{
			value &= 0xF0 | Joypad.directions;
		}

		return value;
	}

	if (addr == 0xFF69) //read CGB BGPD
	{
		return cgb_BGP[Memory[0xFF68] & 0x3F];
	}

	if (addr == 0xFF6B) //read CGB OBPD
	{
		return cgb_OBP[Memory[0xFF6A] & 0x3F];
	}

	return Memory[addr]; //just return the mapped memory
}

void Mmu::WriteByte(uint16_t addr, uint8_t val)
{
	if (addr < 0x8000) // ROM area. todo: should be handled by the MBC
	{
		switch (currentMBC)
		{
		case(MBC_TYPE::MBC1):
			WriteMBC1(addr, val);
			return;
		case(MBC_TYPE::MBC2):
			WriteMBC2(addr, val);
			return;
		case(MBC_TYPE::MBC3):
			WriteMBC3(addr, val);
			return;
		case(MBC_TYPE::MBC5):
			WriteMBC5(addr, val);
			return;
		case(MBC_TYPE::NOMBC):
		case(MBC_TYPE::UNKNOWN):
		default:
			//std::cout << "Blocked write to 0x" << std::hex << std::setfill('0') << std::uppercase << std::setw(4) << addr << std::endl;
			return;
		}
	}

	if (addr > 0x7FFF && addr < 0xA000) //VRAM
	{
		VRAM[currentVRAMBank][addr & 0x1FFF] = val;
		return;
	}

	if (addr > 0xBFFF && addr < 0xD000) //WRAM bank 0
	{
		WRAM[0][addr & 0x0FFF] = val;
		return;
	}
	if (addr > 0xCFFF && addr < 0xE000) //WRAM high bank (cgb mode), WRAM bank 1 in DMG mode
	{
		WRAM[currentWRAMBank][addr & 0x0FFF] = val;
		return;
	}

	if (addr >= 0xFE00 && addr <= 0xFE9F) // OAM
	{
		Memory[addr] = val;
		return;
	}

	if (addr == 0xFF00) //joypad input
	{
		uint8_t joypadSelect = val & 0x30; //get only the 2 bits for mode select
		Memory[0xFF00] &= 0xCF; // clear the 2 select bits. 0xFF00 & 1100 1111
		Memory[0xFF00] |= joypadSelect; //set the new joypad mode
		return;
	}
	
	if (addr == 0xFF01) //TODO: implement serial. Stubbing serial output for now in order to read the results of blargg's test roms
	{
		//std::cout << (char)val << std::flush;
		Memory[addr] = val;
		return;
	}
	if (addr == 0xFF02)
	{
		if (val == 0x81)
		{
			std::cout << (char)Memory[0xFF01] << std::flush;
			Memory[0xFF01] = 0x00;
		}
		return;
	}

	if (addr == 0xFF04) //DIV timer register, set to 0 on write
	{
		master_clock = 0;
		Memory[0xFF04] = 0;
		return;
	}

	if (addr == 0xFF05) //tima
	{
		Memory[0xFF05] = val;
		return;
	}

	if (addr == 0xFF41) //lcd stat
	{
		Memory[0xFF41] = (val & 0xF8) | (Memory[0xFF41] & 0x07); //mask off the bottom 3 bits which are read only
		return;
	}

	if (addr == 0xFF44) //LY, read only
		return;

	if (addr == 0xFF46) //DMA control
	{
		Memory[0xFF46] = val;
		DMABaseAddr = val << 8;
		DMACycles = 0;
		DMAInProgress = true;
		return;
	}

	if (addr == 0xFF4F)
	{
		currentVRAMBank = val & 0x01;
		Memory[0xFF4F] = 0xFE | currentVRAMBank;
	}

	if (addr == 0xFF50) //DMG Bootrom enable. Zero on startup. Non-zero disables bootrom
	{
		if (bootRomEnabled && (val & 0x01))
		{
			bootRomEnabled = false;
			Memory[0xFF50] = 0x01;
		}
		return;
	}

	if (addr == 0xFF69) //write to CGB BGPD
	{
		cgb_BGP[Memory[0xFF68] & 0x3F] = val;
		if (Memory[0xFF68] & 0x80)
		{
			Memory[0xFF68] = (((Memory[0xFF68] & 0x3F) + 1) & 0x3F) | 0x80; //increment palette index
		}
	}

	if (addr == 0xFF6B) //write to CGB OBPD
	{
		cgb_OBP[Memory[0xFF6A] & 0x3F] = val;
		if (Memory[0xFF6A] & 0x80)
		{
			Memory[0xFF6A] = (((Memory[0xFF6A] & 0x3F) + 1) & 0x3F) | 0x80; //increment palette index
		}
	}

	if (addr == 0xFF70) //wram bank (cgb only)
	{
		currentWRAMBank = val & 0x07;
		if (currentWRAMBank == 0)
			currentWRAMBank = 1;
	}

	if (addr > 0xDFFF && addr < 0xFE00) //echo ram
	{
		Memory[addr - 0x2000] = val;
		return;
	}

	if (addr > 0xFE9F && addr < 0xFF00) //prohibited area
		return;

	if (addr >= 0xA000 && addr < 0xC000) //external cartridge ram
	{
		if (currentMBC == MBC1 && isCartRamEnabled)
		{
			if (totalRamBanks > 1 && mbc1Mode == 1)
				WriteCartRam(((uint16_t)(hiBank) << 13) + (addr & 0x1FFF), val);
			else if (totalRamBanks)
				WriteCartRam((addr & 0x1FFF), val);
		}
		if (currentMBC == MBC2 && isCartRamEnabled)
		{
			WriteCartRam((addr & 0x01FF), ((val & 0x0F) | 0xF0));
		}
		if (currentMBC == MBC3)
		{
			if (doesRTCExist && isRTCEnabled && (mappedRTCReg < RTCREGS::NONE))
			{
				switch (mappedRTCReg)
				{
				case(RTCREGS::S):
					rtc_ticks = 0;
				case(RTCREGS::M):
					rtcRegValues[mappedRTCReg] = val & 0x3F;
					break;
				case(RTCREGS::H):
					rtcRegValues[mappedRTCReg] = val & 0x1F;
					break;
				case(RTCREGS::DL):
					rtcRegValues[mappedRTCReg] = val;
					break;
				case(RTCREGS::DH):
					rtcRegValues[mappedRTCReg] = val & 0xC1;
					break;
				default:
					break;
				}
			}
			else if (isCartRamEnabled)
			{
				if (totalRamBanks > 1)
					WriteCartRam(((uint16_t)(hiBank) << 13) + (addr & 0x1FFF), val);
				else if (totalRamBanks)
					WriteCartRam((addr & 0x1FFF), val);
			}
		}
		if (currentMBC == MBC5 && isCartRamEnabled)
		{
			if (totalRamBanks > 1)
				WriteCartRam(((uint16_t)(currentRamBank) << 13) + (addr & 0x1FFF), val);
			else if (totalRamBanks)
				WriteCartRam((addr & 0x1FFF), val);
		}
		return;
	}

	if(addr > 0x7FFF)
		Memory[addr] = val;

	return;
}

uint16_t Mmu::ReadWord(uint16_t addr)
{
	return (uint16_t)((ReadByteDirect(addr + 1) << 8) | ReadByteDirect(addr)); //return the 16bit word at addr, flip from little endian to big endian
}

void Mmu::WriteWord(uint16_t addr, uint16_t val)
{
	//write the word to memory, flipping to little endian
	WriteByte(addr + 1, (uint8_t)(val >> 8));
	WriteByte(addr, (uint8_t)(val & 0xFF));
}

uint8_t* Mmu::GetROM()
{
	return &ROM[0];
}

uint8_t* Mmu::GetDMGBootRom()
{
	return &DMGBootROM[0];
}

uint8_t* Mmu::GetCGBBootRom()
{
	return &CGBBootROM[0];
}

void Mmu::SetCGBMode(bool enableCGB)
{
	cgbMode = enableCGB;
}

bool Mmu::GetCGBMode()
{
	return cgbMode;
}

uint8_t Mmu::ReadVRAMDirect(uint16_t addr, uint8_t bank)
{
	return VRAM[bank][addr & 0x1FFF];
}

uint32_t Mmu::GetBGPColor(uint8_t paletteNum, uint8_t index)
{
	//todo: maybe clean this up so it's less ugly
	uint16_t nativeColor = (cgb_BGP[(paletteNum * 8) + (index * 2) + 1] << 8) | cgb_BGP[(paletteNum * 8) + (index * 2)];
	uint32_t finalColor = 0xFF000000;
	finalColor |= (((nativeColor & 0x1F) << 3) | ((nativeColor & 0x1F) >> 2)); //red
	finalColor |= ((((nativeColor >> 5) & 0x1F) << 3) | (((nativeColor >> 5)  & 0x1F) >> 2)) << 8; //green
	finalColor |= ((((nativeColor >> 10) & 0x1F) << 3) | (((nativeColor >> 10) & 0x1F) >> 2)) << 16; //blue
	return finalColor;
}

uint32_t Mmu::GetOBPColor(uint8_t paletteNum, uint8_t index)
{
	//todo: maybe clean this up so it's less ugly
	uint16_t nativeColor = (cgb_OBP[(paletteNum * 8) + (index * 2) + 1] << 8) | cgb_OBP[(paletteNum * 8) + (index * 2)];
	uint32_t finalColor = 0xFF000000;
	finalColor |= (((nativeColor & 0x1F) << 3) | ((nativeColor & 0x1F) >> 2)); //red
	finalColor |= ((((nativeColor >> 5) & 0x1F) << 3) | (((nativeColor >> 5) & 0x1F) >> 2)) << 8; //green
	finalColor |= ((((nativeColor >> 10) & 0x1F) << 3) | (((nativeColor >> 10) & 0x1F) >> 2)) << 16; //blue
	return finalColor;
}

bool Mmu::isBootRomEnabled()
{
	return bootRomEnabled;
}

bool Mmu::isDMAInProgress()
{
	return DMAInProgress;
}

//bypass safety and write directly to address in memory
void Mmu::WriteByteDirect(uint16_t addr, uint8_t val)
{
	Memory[addr] = val;
}

void Mmu::ParseRomHeader(const std::string& romFileName)
{
	uint8_t cart_type = ROM[0x0147];
	uint8_t rom_size  = ROM[0x0148];
	uint8_t ram_size  = ROM[0x0149];

	switch (cart_type)
	{
	case(0x00):
	case(0x08):
	case(0x09):
		currentMBC = MBC_TYPE::NOMBC;
		break;
	case(0x03):
		hasSaveBattery = true;
	case(0x01):
	case(0x02):
		currentMBC = MBC_TYPE::MBC1;
		break;
	case(0x06):
		hasSaveBattery = true;
	case(0x05):
		currentMBC = MBC_TYPE::MBC2;
		break;
	case(0x10):
	case(0x0F):
		doesRTCExist = true;
	case(0x13):
		hasSaveBattery = true;
	case(0x11):
	case(0x12):
		currentMBC = MBC_TYPE::MBC3;
		break;
	case(0x1B):
	case(0x1E):
		hasSaveBattery = true;
	case(0x19):
	case(0x1A):
	case(0x1C):
	case(0x1D):
		currentMBC = MBC_TYPE::MBC5;
		break;
	default:
		currentMBC = MBC_TYPE::UNKNOWN; //several weird tpyes that I'm not even going to try to support
		break;
	}

	switch (rom_size)
	{
	case(0x01):
		totalRomBanks = 4;
		break;
	case(0x02):
		totalRomBanks = 8;
		break;
	case(0x03):
		totalRomBanks = 16;
		break;
	case(0x04):
		totalRomBanks = 32;
		break;
	case(0x05):
		totalRomBanks = 64;
		break;
	case(0x06):
		totalRomBanks = 128;
		break;
	case(0x07):
		totalRomBanks = 256;
		break;
	case(0x08):
		totalRomBanks = 512;
		break;
	case(0x00):
	default:
		totalRomBanks = 2;
		break;
	}

	switch (ram_size)
	{
	case(0x02):
		totalRamBanks = 1;
		break;
	case(0x03):
		totalRamBanks = 4;
		break;
	case(0x04):
		totalRamBanks = 16;
		break;
	case(0x05):
		totalRamBanks = 8;
		break;
	case(0x00):
	case(0x01):
	default:
		if (currentMBC == MBC2)
			totalRamBanks = 1;
		else
			totalRamBanks = 0;
		break;
	}

	if (totalRamBanks)
	{
		if (hasSaveBattery)
		{
			LoadSave(romFileName);
		}
		else
		{
			if (CartRam.size() > 0)
				CartRam.clear();
			CartRam.resize(0x2000 * totalRamBanks);
		}
	}

	return;
}

void Mmu::LoadSave(const std::string& romFileName)
{
	if (!hasSaveBattery)
		return;

	uint32_t savesize = 0x2000 * totalRamBanks;

	if (doesRTCExist)
		savesize += 48;

	saveFileName = NewFileExtension(romFileName, "sav");

	if (FileExists(saveFileName))
	{
		if (FileSize(saveFileName) != savesize)
		{
			//TODO: fill save data with FF instead of 00
			ResizeFile(saveFileName, savesize);
		}

		CartRam.clear();
		CartRam.reserve(savesize);

		std::ifstream inFile;
		inFile.open(saveFileName, std::ios::in | std::ios::binary);
		inFile.unsetf(std::ios::skipws);
		inFile.seekg(0, std::ios::end);
		int fileSize = inFile.tellg();
		inFile.seekg(0, std::ios::beg);
		if (!inFile.good())
		{
			std::cout << "Error opening save file: " << saveFileName << std::endl;
			return;
		}
		if (fileSize != savesize)
		{
			std::cout << "Save File size error. Expected: " << (int)savesize << " Actual: " << (int)fileSize << std::endl;
		}

		std::copy(std::istream_iterator<unsigned char>(inFile), std::istream_iterator<unsigned char>(), std::back_inserter(CartRam));

		if (doesRTCExist)
		{

			rtcRegValues[0] = CartRam[CartRam.size() - 48];
			rtcRegValues[1] = CartRam[CartRam.size() - 44];
			rtcRegValues[2] = CartRam[CartRam.size() - 40];
			rtcRegValues[3] = CartRam[CartRam.size() - 36];
			rtcRegValues[4] = CartRam[CartRam.size() - 32];

			latchedRtcRegValues[0] = CartRam[CartRam.size() - 28];
			latchedRtcRegValues[1] = CartRam[CartRam.size() - 24];
			latchedRtcRegValues[2] = CartRam[CartRam.size() - 20];
			latchedRtcRegValues[3] = CartRam[CartRam.size() - 16];
			latchedRtcRegValues[4] = CartRam[CartRam.size() - 12];

			uint64_t old_timestamp;
			std::memcpy(&old_timestamp, &CartRam[CartRam.size() - sizeof(old_timestamp)], sizeof(old_timestamp));

			auto tp = std::chrono::system_clock::now();
			auto dur = tp.time_since_epoch();
			auto seconds = std::chrono::duration_cast<std::chrono::seconds>(dur).count();
			uint64_t current_timestamp = (uint64_t)seconds;

			//todo: this rtc code is a dumpster fire. fix this nonsense

			if (old_timestamp < current_timestamp)
			{
				uintmax_t seconds_to_add = current_timestamp - old_timestamp;
				
				if (seconds_to_add)
				{
					while(seconds_to_add >= 86400) //add days
					{
						if (rtcRegValues[DL] == 0xFF) //days overflow
							rtcRegValues[DH] |= 1;
						rtcRegValues[DL] += 1;
						seconds_to_add -= 86400;
					}
					while (seconds_to_add >= 3600) //add hours
					{
						if (rtcRegValues[H] == 23) //hours overflow
						{
							if (rtcRegValues[DL] == 0xFF) //days overflow
								rtcRegValues[DH] |= 1;
							rtcRegValues[DL] += 1;
						}
						rtcRegValues[H] += 1;
						seconds_to_add -= 3600;
					}
					while (seconds_to_add >= 60)
					{
						if (rtcRegValues[M] == 59)
						{
							if (rtcRegValues[H] == 23) //hours overflow
							{
								if (rtcRegValues[DL] == 0xFF) //days overflow
									rtcRegValues[DH] |= 1;
								rtcRegValues[DL] += 1;
							}
							rtcRegValues[H] += 1;
						}
						rtcRegValues[M] += 1;
						seconds_to_add -= 60;
					}
					
					if (rtcRegValues[S] + seconds_to_add > 60)
					{
						if (rtcRegValues[M] == 59) //minutes overflow
						{
							if (rtcRegValues[H] == 23) //hours overflow
							{
								if (rtcRegValues[DL] == 0xFF) //days overflow
									rtcRegValues[DH] |= 1;
								rtcRegValues[DL] += 1;
							}
							rtcRegValues[H] += 1;
						}
						rtcRegValues[M] += 1;
					}

					rtcRegValues[S] = (rtcRegValues[S] + seconds_to_add) % 60;
				}
			}

			CartRam.resize(savesize - 48);
		}
		

		inFile.close();
	}
	else
	{
		if (doesRTCExist)
			CartRam.resize(savesize - 48, 0xFF);
		else
			CartRam.resize(savesize, 0xFF);
		//std::fill(CartRam.begin(), CartRam.end(), 0xFF);
		std::ofstream saveFstream;
		saveFstream.open(saveFileName, std::ios::out | std::ios::binary | std::ios::beg);
		saveFstream.write((char*)&CartRam[0], CartRam.size() * sizeof(uint8_t));
		saveFstream.close();
		ResizeFile(saveFileName, savesize);
	}

	return;
}

void Mmu::SaveGame(const std::string& romFileName)
{
	if (!hasSaveBattery)
		return;

	uint32_t savesize = 0x2000 * totalRamBanks;

	saveFileName = NewFileExtension(romFileName, "sav");

	std::ofstream saveFstream;
	saveFstream.open(saveFileName, std::ios::out | std::ios::binary | std::ios::beg);
	saveFstream.write((char*)&CartRam[0], CartRam.size() * sizeof(uint8_t));
	
	
	if (doesRTCExist)
	{
		
		//TODO: actually write RTC to end in 48 byte format
		for (int i = 0; i < 5; i++)
		{
			uint32_t tempVal = rtcRegValues[i];
			saveFstream.write((char*)&tempVal, sizeof(uint32_t));
		}
		for (int i = 0; i < 5; i++)
		{
			uint32_t tempVal = latchedRtcRegValues[i];
			saveFstream.write((char*)&tempVal, sizeof(uint32_t));
		}
		auto tp = std::chrono::system_clock::now();
		auto dur = tp.time_since_epoch();
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(dur).count();
		uint64_t timestamp = (uint64_t)seconds;
		saveFstream.write((char*)&timestamp, sizeof(uint64_t));
		saveFstream.close();
		savesize += 48;
		ResizeFile(saveFileName, savesize);
	}		

	return;
}

uint8_t Mmu::ReadCartRam(uint16_t addr)
{
	if (CartRam.size() >= addr + 1)
		return CartRam[addr];

	std::cout << "Error reading from Cart Ram: " << std::hex << std::setfill('0') << std::uppercase << std::setw(4) << (int)addr << std::endl;
	return 0xFF;
}
void Mmu::WriteCartRam(uint16_t addr, uint8_t val)
{
	if (CartRam.size() >= addr + 1)
	{
		CartRam[addr] = val;
		return;
	}

	std::cout << "Error writing to Cart Ram: " << std::hex << std::setfill('0') << std::uppercase << std::setw(4) << (int)addr << std::endl;
	return;
}

void Mmu::WriteMBC1(uint16_t addr, uint8_t val)
{
	if (addr < 0x2000) //0x0000 to 0x1FFF cartridge ram enable/disable
	{
		uint8_t enable = val & 0x0F;
		if (enable == 0x0A)
			isCartRamEnabled = true;
		else
			isCartRamEnabled = false;
		return;
	}
	if (addr < 0x4000) //0x2000 to 0x3FFF rom bank number
	{
		lowBank = val & 0x1F; //TODO: there's a much more clever way to do this bitmask but i'm sleepy
		if(lowBank == 0x00)
			lowBank = 0x01;
		if (totalRomBanks <= 16 && totalRomBanks > 8)
			lowBank &= 0x0F;
		else if (totalRomBanks <= 8 && totalRomBanks > 4)
			lowBank &= 0x07;
		else if (totalRomBanks <= 4)
			lowBank &= 0x03;

		if (mbc1Mode == 0 && totalRomBanks > 0x20)
			currentRomBank = (hiBank << 5) | lowBank;
		else
			currentRomBank = lowBank;
		return;
	}
	if (addr < 0x6000) // RAM bank number / upper rom bank number
	{
		hiBank = val & 0x03;

		if (mbc1Mode == 0 && totalRomBanks > 0x20)
		{
			currentRomBank = (hiBank << 5) | lowBank;
		}
		else if (mbc1Mode == 1 && totalRamBanks > 1)
			currentRamBank = hiBank;
		else if (mbc1Mode == 1 && totalRomBanks > 0x20)
		{
			currentRomBank = (hiBank << 5) | lowBank;
		}

		return;
	}
	if (addr < 0x8000) //bank mode select
	{
		mbc1Mode = val & 0x01;
	}
	return;
}

void Mmu::WriteMBC2(uint16_t addr, uint8_t val)
{
	if (addr < 0x4000)
	{
		if (addr & 0x0100) //change rom bank number
		{
			currentRomBank = val & 0x0F;
			if (currentRomBank == 0)
				currentRomBank = 1;
		}
		else //enable/disable RAM
		{
			if ((val & 0x0F) == 0x0A)
				isCartRamEnabled = true;
			else
				isCartRamEnabled = false;
		}
	}
	return;
}

void Mmu::WriteMBC3(uint16_t addr, uint8_t val)
{
	if (addr < 0x2000) //0x0000 to 0x1FFF cartridge ram enable/disable
	{
		uint8_t enable = val & 0x0F;
		if (enable == 0x0A)
		{
			isCartRamEnabled = true;
			isRTCEnabled = true;
		}
		else
		{
			isCartRamEnabled = false;
			isRTCEnabled = false;
		}
		return;
	}
	if (addr < 0x4000) //0x2000 to 0x3FFF rom bank number
	{
		lowBank = val & 0x7F;
		if (lowBank == 0x00)
			lowBank = 0x01;

		currentRomBank = lowBank;
		return;
	}
	if (addr < 0x6000) // RAM bank number / rtc register select
	{
		if (val >= 0x08 && val <= 0x0C)
		{
			mappedRTCReg = (RTCREGS)(val - 0x08);
		}
		else if (val < 4)
		{
			hiBank = val;
			currentRamBank = hiBank;
			mappedRTCReg = RTCREGS::NONE;
		}


		return;
	}
	if (addr < 0x8000) //latch clock data
	{
		//TODO: latch clock data if writing 01 when current value is 00
		if (val == 0x01 && rtcLatchRegs == 0x00)
		{
			isRTCLatched = !isRTCLatched;

			if (isRTCLatched)
			{
				latchedRtcRegValues[S]  = rtcRegValues[S];
				latchedRtcRegValues[M]  = rtcRegValues[M];
				latchedRtcRegValues[H]  = rtcRegValues[H];
				latchedRtcRegValues[DL] = rtcRegValues[DL];
				latchedRtcRegValues[DH] = rtcRegValues[DH];
			}
		}
		rtcLatchRegs = val; //val & 0x01;
	}
	return;
}

void Mmu::WriteMBC5(uint16_t addr, uint8_t val)
{
	if (addr < 0x2000) //0x0000 to 0x1FFF cartridge ram enable/disable
	{
		if (val == 0x0A)
			isCartRamEnabled = true;
		else
			isCartRamEnabled = false;
		return;
	}
	if (addr < 0x3000) //0x2000 to 0x3FFF rom bank number
	{
		lowBank = val;

		currentRomBank = (hiBank << 8) | lowBank;
		return;
	}
	if (addr < 0x4000) // RAM bank 9th bit
	{
		hiBank = val & 0x01;
		currentRomBank = (hiBank << 8) | lowBank;

		return;
	}
	if (addr < 0x6000) //ram bank select
	{
		currentRamBank = val & 0x0F;
	}
	return;
}
