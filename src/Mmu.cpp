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

Mmu::Mmu(Apu* __apu, Serial* __lc) : apu(__apu), linkCable(__lc)
{
	//memset(Memory, 0xFF, sizeof(Memory));
	Memory[0xFF00] = 0xFF; //stub initial input to all buttons released
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

	if (linkCable)
	{
		if (linkCable->incomingQueue.size() > 0)
		{
			if (!linkCable->expectingResponse)
			{
				linkCable->outgoingQueue.push(linkCable->SB);
			}
			//not expecting a response
			linkCable->expectingResponse = false;
			//set the new SB data
			uint8_t newData = linkCable->incomingQueue.front();
			linkCable->incomingQueue.pop();
			linkCable->SB = newData;
			Memory[0xFF01] = newData;

			//clear the transfer flag
			Memory[0xFF02] = 0x7C;
			//trigger a serial interrupt
			Memory[0xFF0F] |= 0x08;
		}
	}

	if (rumbleActive)
		rumbleStrength += (cycles / 4);

	//DMG DMA
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
		if (currentPPUMode != 3)
			return VRAM[currentVRAMBank][addr & 0x1FFF];
		else
			std::cout << "Error: Read from VRAM during Mode 3" << std::endl;
		return 0xFF;
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

	if (addr == 0xFF01)
	{
		return Memory[0xFF01];
	}
	if (addr == 0xFF02)
	{
		return Memory[0xFF02];
	}
	if (addr >= 0xFF10 && addr <= 0xFF3F)
	{
		switch (addr)
		{
		case(0xFF10):
			return Memory[addr] | 0x80;
		case(0xFF11):
			return Memory[addr] | 0x3F;
		case(0xFF12):
			return Memory[addr];
		case(0xFF13):
			return 0xFF;
		case(0xFF14):
			return Memory[addr] | 0xBF;
		case(0xFF15):
			return 0xFF;
		case(0xFF16):
			return Memory[addr] | 0x3F;
		case(0xFF17):
			return Memory[addr];
		case(0xFF18):
			return 0xFF;
		case(0xFF19):
			return Memory[addr] | 0xBF;
		case(0xFF1A):
			return Memory[addr] | 0x7F;
		case(0xFF1B):
			return 0xFF;
		case(0xFF1C):
			return Memory[addr] | 0x9F;
		case(0xFF1D):
			return 0xFF;
		case(0xFF1E):
			return Memory[addr] | 0xBF;
		case(0xFF1F):
			return 0xFF;
		case(0xFF20):
			return 0xFF;
		case(0xFF21):
			return Memory[addr];
		case(0xFF22):
			return Memory[addr];
		case(0xFF23):
			return Memory[addr] | 0xBF;
		case(0xFF24):
			return Memory[addr];
		case(0xFF25):
			return Memory[addr];
		case(0xFF26):
			if (apu)
			{
				return apu->GetAudioEnable() | 0x70;
			}
			return Memory[addr] | 0x70;
		case(0xFF27):
		case(0xFF28):
		case(0xFF29):
		case(0xFF2A):
		case(0xFF2B):
		case(0xFF2C):
		case(0xFF2D):
		case(0xFF2E):
		case(0xFF2F):
			return 0xFF;
		case(0xFF30):
		case(0xFF31):
		case(0xFF32):
		case(0xFF33):
		case(0xFF34):
		case(0xFF35):
		case(0xFF36):
		case(0xFF37):
		case(0xFF38):
		case(0xFF39):
		case(0xFF3A):
		case(0xFF3B):
		case(0xFF3C):
		case(0xFF3D):
		case(0xFF3E):
		case(0xFF3F):
			return Memory[addr];
		default:
			return 0xFF;
		}
	}

	if (addr == 0xFF40)
	{
		return Memory[addr];
	}

	if (addr == 0xFF41)
	{
		return Memory[addr];
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
		if (currentPPUMode != 3)
			VRAM[currentVRAMBank][addr & 0x1FFF] = val;
		else
			std::cout << "Error: write to VRAM during Mode 3" << std::endl;
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
	
	if (addr == 0xFF01)
	{
		//std::cout << (char)val << std::flush;
		Memory[addr] = val;
		if (linkCable)
			linkCable->SB = val;
		return;
	}
	if (addr == 0xFF02)
	{
		if ((val & 0x81) == 0x81)
		{
			if (linkCable)
			{
				if (linkCable->IsConnected())
				{
					if (!linkCable->expectingResponse)
					{
						linkCable->expectingResponse = true;
						linkCable->outgoingQueue.push(linkCable->SB);
					}
				}
				else
				{
					linkCable->incomingQueue.push(0xFF);
				}
			}
			else
			{
				std::cout << (char)Memory[0xFF01] << std::flush;
				Memory[0xFF01] = 0xFF;
			}
		}
		Memory[addr] = cgbSupport ? (val | 0x7C) : (val | 0x7E);
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

	if (addr >= 0xFF10 && addr <= 0xFF2F) //apu regs
	{
		if (addr == 0xFF26)
		{
			//Bit 7 - All sound on / off(0: stop all sound circuits) (Read / Write)
			//Bit 3 - Sound 4 ON flag(Read Only)
			//Bit 2 - Sound 3 ON flag(Read Only)
			//Bit 1 - Sound 2 ON flag(Read Only)
			//Bit 0 - Sound 1 ON flag(Read Only)
			if (val & 0x80)
			{
				if (apu) { apu->SetAudioEnable(true); }

				Memory[0xFF26] |= 0x80;
			}
			else
			{
				if (apu) { apu->SetAudioEnable(false); }
				for (int i = 0xFF10; i < 0xFF26; i++)
					WriteByte(i, 0x00);
				for (int i = 0xFF27; i < 0xFF30; i++)
					WriteByte(i, 0x00);
				Memory[0xFF26] = 0x00;
			}
			return;
		}
		if (!apu || (((apu->GetAudioEnable() & 0x80) != 0x80) && val != 0x00))
			return;
		switch (addr)
		{
		case(0xFF10):
			if (apu) { apu->ChannelOneSetSweep(val); }
			Memory[0xFF10] = val | 0x80;
			return;
		case(0xFF11):
			if (apu) { apu->ChannelOneSetLength(val); }
			Memory[0xFF11] = val | 0x3F;
			return;
		case(0xFF12):
			if (apu) { apu->ChannelOneSetVolume(val); }
			Memory[0xFF12] = val;
			return;
		case(0xFF13):
			//Frequency's lower 8 bits of 11 bit data (x). Next 3 bits are in NR24 ($FF19).
			if (apu) { apu->ChannelOneSetFreq(val); }
			Memory[0xFF13] = val;
			return;
		case(0xFF14):
			if (apu) { apu->ChannelOneTrigger(val); }
			Memory[0xFF14] = val | 0xBF;
			return;
		case(0xFF15):
			Memory[addr] = val;
			return;
		case(0xFF16):
			if (apu) { apu->ChannelTwoSetLength(val); }
			Memory[0xFF16] = val | 0x3F;
			return;
		case(0xFF17):
			if (apu) { apu->ChannelTwoSetVolume(val); }
			Memory[0xFF17] = val;
			return;
		case(0xFF18):
			//Frequency's lower 8 bits of 11 bit data (x). Next 3 bits are in NR24 ($FF19).
			if (apu) { apu->ChannelTwoSetFreq(val); }
			Memory[0xFF18] = val;
			return;
		case(0xFF19):
			if (apu) { apu->ChannelTwoTrigger(val); }
			Memory[0xFF19] = (val & 0x40) | 0xBF;
			return;
		case(0xFF1A):
			if (apu) { apu->ChannelThreeSetEnable(val); }
			Memory[0xFF1A] = val | 0x7F;
			return;
		case(0xFF1B):
			if (apu) { apu->ChannelThreeSetLength(val); }
			Memory[0xFF1B] = val;
			return;
		case(0xFF1C):
			if (apu) { apu->ChannelThreeSetVolume(val); }
			Memory[0xFF1C] = val | 0x9F;
			return;
		case(0xFF1D):
			if (apu) { apu->ChannelThreeSetFreq(val); }
			Memory[0xFF1D] = val;
			return;
		case(0xFF1E):
			if (apu) { apu->ChannelThreeTrigger(val); }
			Memory[0xFF1E] = val | 0xBF;
			return;
		case(0xFF1F):
			Memory[addr] = val;
			return;
		case(0xFF20):
			if (apu) { apu->ChannelFourSetLength(val); }
			Memory[addr] = val | 0xC0;
			return;
		case(0xFF21):
			if (apu) { apu->ChannelFourSetVolume(val); }
			Memory[addr] = val;
			return;
		case(0xFF22):
			if (apu) { apu->ChannelFourSetPoly(val); }
			Memory[addr] = val;
			return;
		case(0xFF23):
			if (apu) { apu->ChannelFourTrigger(val); }
			Memory[addr] = val | 0xBF;
			return;
		case(0xFF24):
			if (apu) { apu->SetMasterVolume(val); }
			Memory[addr] = val;
			return;
		case(0xFF25):
			if (apu) { apu->SetPan(val); }
			Memory[addr] = val;
			return;
		case(0xFF27):
			Memory[addr] = val;
			return;
		case(0xFF28):
			Memory[addr] = val;
			return;
		case(0xFF29):
			Memory[addr] = val;
			return;
		case(0xFF2A):
			Memory[addr] = val;
			return;
		case(0xFF2B):
			Memory[addr] = val;
			return;
		case(0xFF2C):
			Memory[addr] = val;
			return;
		case(0xFF2D):
			Memory[addr] = val;
			return;
		case(0xFF2E):
			Memory[addr] = val;
			return;
		case(0xFF2F):
			Memory[addr] = val;
			return;

		default:
			return;
		}
	}

	if (addr >= 0xFF30 && addr <= 0xFF3F) //wave ram
	{
		if (apu) { apu->SetWaveRam(addr & 0x000F, val); }
		Memory[addr] = val;
		return;
	}

	if (addr == 0xFF40)
	{
		Memory[0xFF40] = val;
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

	if (addr == 0xFF4D) //prep speed switch. cgb only
	{
		Memory[0xFF4D] &= 0x80;
		Memory[0xFF4D] |= 0x7E;
		Memory[0xFF4D] |= (val & 0x01);
		return;
	}

	if (addr == 0xFF4F)
	{
		currentVRAMBank = val & 0x01;
		Memory[0xFF4F] = 0xFE | currentVRAMBank;
		return;
	}

	if (addr == 0xFF50) //Bootrom enable. Zero on startup. Non-zero disables bootrom
	{
		if (bootRomEnabled && (val & 0x01))
		{
			bootRomEnabled = false;
			Memory[0xFF50] = 0xFF;

			//if (cgbMode && (!cgbSupport))
			//{
			//	cgbMode = false;
			//}
		}
		return;
	}

	if (addr == 0xFF51) //HDMA1 Src Addr High Byte
	{
		Memory[0xFF51] = val;
		return;
	}

	if (addr == 0xFF52) //HDMA2 Src Addr Low Byte
	{
		Memory[0xFF52] = val & 0xF0;
		return;
	}

	if (addr == 0xFF53) //HDMA3 Dest High Byte
	{
		Memory[0xFF53] = val & 0x1F;
		return;
	}

	if (addr == 0xFF54) //HDMA4 Dest Low Byte
	{
		Memory[0xFF54] = val & 0xF0;
		return;
	}

	if (addr == 0xFF55) //HDMA5 start/stop/length
	{
		//Memory[0xFF55] = val;
		if (val & 0x80) //start h-blank dma
		{
			HDMAInProgress = true;
			HDMATransferredTotal = 0;
			HDMATransferredThisLine = 0;
			HDMALength = ((val & 0x7F) + 1) << 4;
			HDMASrcAddr = ((uint16_t)Memory[0xFF51] << 8) | Memory[0xFF52];
			HDMADestAddr = (((uint16_t)Memory[0xFF53] << 8) | Memory[0xFF54]) & 0x1FFF;
			Memory[0xFF55] = val & 0x7F;
			return;
		}
		else
		{
			if (HDMAInProgress) //stop h-blank dma
			{
				HDMAInProgress = false;
				Memory[0xFF55] |= 0x80;
				return;
			}
			else //perform general dma
			{
				uint16_t src = ((uint16_t)Memory[0xFF51] << 8) | Memory[0xFF52];
				uint16_t dest = (((uint16_t)Memory[0xFF53] << 8) | Memory[0xFF54]) & 0x1FFF;

				uint16_t length = ((val & 0x7F) + 1) << 4;

				for (uint16_t i = 0; i < length; i++)
				{
					VRAM[currentVRAMBank][dest + i] = ReadByteDirect(src + i);
				}
				Memory[0xFF55] = 0xFF;
				return;
			}
		}
	}

	if (addr == 0xFF69) //write to CGB BGPD
	{
		cgb_BGP[Memory[0xFF68] & 0x3F] = val;
		if (Memory[0xFF68] & 0x80)
		{
			Memory[0xFF68] = (((Memory[0xFF68] & 0x3F) + 1) & 0x3F) | 0x80; //increment palette index
		}
		return;
	}

	if (addr == 0xFF6B) //write to CGB OBPD
	{
		cgb_OBP[Memory[0xFF6A] & 0x3F] = val;
		if (Memory[0xFF6A] & 0x80)
		{
			Memory[0xFF6A] = (((Memory[0xFF6A] & 0x3F) + 1) & 0x3F) | 0x80; //increment palette index
		}
		return;
	}

	if (addr == 0xFF70) //wram bank (cgb only)
	{
		currentWRAMBank = val & 0x07;
		if (currentWRAMBank == 0)
			currentWRAMBank = 1;

		Memory[0xFF70] = 0xF8 | currentWRAMBank;
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

bool Mmu::GetCGBSupport()
{
	return cgbSupport;
}

uint8_t Mmu::ReadVRAMDirect(uint16_t addr, uint8_t bank)
{
	return VRAM[bank][addr & 0x1FFF];
}

uint32_t Mmu::GetBGPColor(uint8_t paletteNum, uint8_t index)
{
	//todo: maybe clean this up so it's less ugly
	uint16_t nativeColor = (cgb_BGP[(paletteNum * 8) + (index * 2) + 1] << 8) | cgb_BGP[(paletteNum * 8) + (index * 2)];
	uint32_t finalColor = 0x000000FF;

	//finalColor |= (((nativeColor & 0x1F) << 3) | ((nativeColor & 0x1F) >> 2)) << 24; //red
	//finalColor |= ((((nativeColor >> 5) & 0x1F) << 3) | (((nativeColor >> 5)  & 0x1F) >> 2)) << 16; //green
	//finalColor |= ((((nativeColor >> 10) & 0x1F) << 3) | (((nativeColor >> 10) & 0x1F) >> 2)) << 8; //blue

	uint8_t  native_red, native_green, native_blue, uncorr_red, uncorr_green, uncorr_blue;
	uint16_t temp_red, temp_green, temp_blue;

	native_red = (nativeColor & 0x1F);
	native_green = ((nativeColor >> 5) & 0x1F);
	native_blue = ((nativeColor >> 10) & 0x1F);

	uncorr_red = ((native_red << 3) | (native_red >> 2));
	uncorr_green = ((native_green << 3) | (native_green >> 2));
	uncorr_blue = ((native_blue << 3) | (native_blue >> 2));

	temp_red = ((native_red * 26) + (native_green * 4) + (native_blue * 2));
	temp_green = ((native_green * 24) + (native_blue * 8));
	temp_blue = ((native_red * 6) + (native_green * 4) + (native_blue * 22));
	temp_red = std::min((uint16_t)960, temp_red) >> 2;
	temp_green = std::min((uint16_t)960, temp_green) >> 2;
	temp_blue = std::min((uint16_t)960, temp_blue) >> 2;

	finalColor |= (temp_red & 0xFF) << 24;
	finalColor |= (temp_green & 0xFF) << 16;
	finalColor |= (temp_blue & 0xFF) << 8;

	return finalColor;
}

uint32_t Mmu::GetOBPColor(uint8_t paletteNum, uint8_t index)
{
	//todo: maybe clean this up so it's less ugly
	uint16_t nativeColor = (cgb_OBP[(paletteNum * 8) + (index * 2) + 1] << 8) | cgb_OBP[(paletteNum * 8) + (index * 2)];
	uint32_t finalColor = 0x000000FF;

	//finalColor |= (((nativeColor & 0x1F) << 3) | ((nativeColor & 0x1F) >> 2)) << 24; //red
	//finalColor |= ((((nativeColor >> 5) & 0x1F) << 3) | (((nativeColor >> 5) & 0x1F) >> 2)) << 16; //green
	//finalColor |= ((((nativeColor >> 10) & 0x1F) << 3) | (((nativeColor >> 10) & 0x1F) >> 2)) << 8; //blue

	uint8_t  native_red, native_green, native_blue, uncorr_red, uncorr_green, uncorr_blue;
	uint16_t temp_red, temp_green, temp_blue;

	native_red   = (nativeColor & 0x1F);
	native_green = ((nativeColor >> 5) & 0x1F);
	native_blue  = ((nativeColor >> 10) & 0x1F);

	uncorr_red   = ((native_red   << 3) | (native_red   >> 2));
	uncorr_green = ((native_green << 3) | (native_green >> 2));
	uncorr_blue  = ((native_blue  << 3) | (native_blue  >> 2));

	temp_red = ((native_red * 26) + (native_green * 4) + (native_blue * 2));
	temp_green = ((native_green * 24) + (native_blue * 8));
	temp_blue = ((native_red * 6) + (native_green * 4) + (native_blue * 22));
	temp_red = std::min((uint16_t)960, temp_red) >> 2;
	temp_green = std::min((uint16_t)960, temp_green) >> 2;
	temp_blue = std::min((uint16_t)960, temp_blue) >> 2;

	finalColor |= (temp_red & 0xFF) << 24;
	finalColor |= (temp_green & 0xFF) << 16;
	finalColor |= (temp_blue & 0xFF) << 8;

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

bool Mmu::isHDMAInProgress()
{
	return HDMAInProgress;
}

void Mmu::DoHDMATransfer()
{
	uint16_t bytesLeft = HDMALength - HDMATransferredTotal;
	HDMATransferredThisLine = 0;

	if (bytesLeft > 0)
	{
		for (int i = 0; i < 16; i++)
		{
			VRAM[currentVRAMBank][HDMADestAddr + HDMATransferredTotal + i] = ReadByteDirect(HDMASrcAddr + HDMATransferredTotal + i);
		}
		HDMATransferredTotal += 16;
		bytesLeft -= 16;
		if (bytesLeft == 0)
			HDMAInProgress = false;
		Memory[0xFF55] = (bytesLeft >> 4) - 1;
	}

	return;
}

//bypass safety and write directly to address in memory
void Mmu::WriteByteDirect(uint16_t addr, uint8_t val)
{
	Memory[addr] = val;
}

void Mmu::ParseRomHeader(const std::string& romFileName)
{
	uint8_t cgb_check = ROM[0x0143];
	uint8_t sgb_check = ROM[0x0146];
	uint8_t cart_type = ROM[0x0147];
	uint8_t rom_size  = ROM[0x0148];
	uint8_t ram_size  = ROM[0x0149];

	if (((cgb_check & 0xC0) != 0) && ((cgb_check & 0x0C) == 0))
		cgbSupport = true;

	if (sgb_check == 0x03)
		sgbSupport = true;

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
	case(0x1E):
		hasRumble = true;
	case(0x1B):
		hasSaveBattery = true;
		currentMBC = MBC_TYPE::MBC5;
		break;
	case(0x1C):
	case(0x1D):
		hasRumble = true;
	case(0x19):
	case(0x1A):
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

void Mmu::RegisterApu(Apu* which)
{
	apu = which;
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

		currentRomBank = ((hiBank << 8) | lowBank);
		return;
	}
	if (addr < 0x4000) // rom bank 9th bit
	{
		hiBank = val & 0x01;
		currentRomBank = ((hiBank << 8) | lowBank);

		return;
	}
	if (addr < 0x6000) //ram bank select
	{
		currentRamBank = val & 0x0F;

		if (hasRumble)
		{
			rumbleActive = ((val & 0x08) != 0) ? true : false;
			currentRamBank &= 0x07;
		}
	}
	return;
}
