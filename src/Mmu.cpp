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

	//stub input to 0xFF, meaning nothing pressed
	ROM[0xFF00] = 0xFF;
}

uint8_t Mmu::ReadByte(uint16_t addr)
{
	if (addr < 0x100 && bootRomEnabled) //Boot Rom
		return DMGBootROM[addr];
	if(addr < 0x4000) //ROM, Bank 0
		return ROM[addr];
	if (addr < 0x8000) //ROM, bank N
		return ROM[0x4000 * currentRomBank + (addr - 0x4000)];
	if (addr > 0x9FFF && addr < 0xC000) //external cartridge ram (possibly banked)
		return Memory[addr]; //TODO: external ram banks
	if (addr > 0xDFFF && addr < 0xFE00) //echo ram
		return Memory[addr - 0x2000];
	if (addr > 0xFE9F && addr < 0xFF00) // prohibited area. todo: during OAM, return FF and trigger sprite bug. else return 00
		return 0x00; 

	return Memory[addr]; //just return the mapped memory
}

void Mmu::WriteByte(uint16_t addr, uint8_t val)
{
	if (addr < 0x8000) // ROM area. todo: should be handled by the MBC
		return;

	if (addr == 0xFF01) //TODO: implement serial. Stubbing serial output for now in order to read the results of blargg's test roms
	{
		std::cout << (char)val << std::flush;
		Memory[addr] = val;
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
		Memory[0xFF41] = (val & 0xF8); //mask off the bottom 3 bits which are read only
		return;
	}

	if (addr == 0xFF44) //LY, read only
		return;

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

	//TODO: this is not even close to right
	if(addr > 0x7FFF)
		Memory[addr] = val;

	return;
}

uint16_t Mmu::ReadWord(uint16_t addr)
{
	return (uint16_t)((ReadByte(addr + 1) << 8) | ReadByte(addr)); //return the 16bit word at addr, flip from little endian to big endian
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

////special function meant for internal use by the timer update. Correctly persists DIV val instead of writing 0
//void Mmu::SaveDiv(uint8_t val)
//{
//	Memory[0xFF04] = val;
//}
//
////special function meant for use by the ppu. Writes STAT value as given, instead of masking off the bottom read only bits
//void Mmu::SaveStat(uint8_t val)
//{
//	Memory[0xFF41] = val;
//}

//bypass safety and write directly to address in memory
void Mmu::WriteByteDirect(uint16_t addr, uint8_t val)
{
	Memory[addr] = val;
}
