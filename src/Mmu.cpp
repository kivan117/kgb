#include "Mmu.h"
#include <iostream>

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
	if (addr < 0x100 && bootRomEnabled) //Boot Rom
		return DMGBootROM[addr];
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
				return CartRam[(uint32_t)((uint32_t)hiBank << 13) + (addr & 0x1FFF)];
			else if(totalRamBanks)
				return CartRam[(addr & 0x1FFF)];
			return 0xFF;
			break;
		}
		case(MBC3):
		{
			if (totalRamBanks > 1)
				return CartRam[(uint32_t)((uint32_t)hiBank << 13) + (addr & 0x1FFF)];
			else if (totalRamBanks)
				return CartRam[(addr & 0x1FFF)];
			return 0xFF;
			break;
		case(MBC5):
		{
			if (totalRamBanks > 1)
				return CartRam[(uint32_t)((uint32_t)currentRamBank << 13) + (addr & 0x1FFF)];
			else if (totalRamBanks)
				return CartRam[(addr & 0x1FFF)];
			return 0xFF;
			break;
		}
		}
		default:
			break;
		}
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

	if (addr == 0xFF50) //DMG Bootrom enable. Zero on startup. Non-zero disables bootrom
	{
		if (bootRomEnabled && (val & 0x01))
		{
			bootRomEnabled = false;
			Memory[0xFF50] = 0x01;
		}
		return;
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
				CartRam[((uint16_t)(hiBank) << 13) + (addr & 0x1FFF)] = val;
			else if(totalRamBanks)
				CartRam[(addr & 0x1FFF)] = val;
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
					CartRam[((uint16_t)(hiBank) << 13) + (addr & 0x1FFF)] = val;
				else if (totalRamBanks)
					CartRam[(addr & 0x1FFF)] = val;
			}
		}
		if (currentMBC == MBC5 && isCartRamEnabled)
		{
			if (totalRamBanks > 1)
				CartRam[((uint16_t)(currentRamBank) << 13) + (addr & 0x1FFF)] = val;
			else if (totalRamBanks)
				CartRam[(addr & 0x1FFF)] = val;
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

void Mmu::ParseRomHeader()
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
	case(0x01):
	case(0x02):
	case(0x03):
		currentMBC = MBC_TYPE::MBC1;
		break;
	case(0x05):
	case(0x06):
		currentMBC = MBC_TYPE::MBC2;
		break;
	case(0x0F):
	case(0x10):
		doesRTCExist = true;
	case(0x11):
	case(0x12):
	case(0x13):
		currentMBC = MBC_TYPE::MBC3;
		break;
	case(0x19):
	case(0x1A):
	case(0x1B):
	case(0x1C):
	case(0x1D):
	case(0x1E):
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
		totalRamBanks = 0;
		break;
	}

	if (totalRamBanks)
	{
		if (CartRam.size())
			CartRam.clear();
		CartRam.resize(1024 * 8 * totalRamBanks);
	}

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
