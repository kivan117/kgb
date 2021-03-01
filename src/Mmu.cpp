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
	if (addr > 0xDFFF && addr < 0xFE00) //echo ram
		return Memory[addr - 0x2000];
	if (addr > 0xFE9F && addr < 0xFF00)
		return 0x00; //TODO: prohibited area. during OAM, return FF and trigger sprite bug. else return 00

	return Memory[addr]; //just return the mapped memory
}

void Mmu::WriteByte(uint16_t addr, uint8_t val)
{
	//there should be oodles of logic here since a "write" to a location is used to toggle all kinds of things on the gameboy
	
	if (addr == 0xFF01) //TODO: implement serial. Stubbing serial output for now in order to read the results of blargg's test roms
	{
		std::cout << (char)val << std::flush;
		Memory[addr] = val;
		return;
	}

	if (addr == 0xFF04) //DIV timer register, set to 0 on write
	{
		Memory[addr] = 0;
		return;
	}

	//if(addr == 0xFF0F)
	//{
	//	std::cout << "Set IF: " << (int)val << std::endl;
	//	Memory[addr] = val;
	//}

	//if (addr == 0xFFFF)
	//{
	//	std::cout << "Set IE: " << (int)val << std::endl;
	//	Memory[addr] = val;
	//}

	//TODO: this is not even close to right
	if(addr > 0x7FFF)
		Memory[addr] = val;
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

bool Mmu::isBootRomEnabled()
{
	return bootRomEnabled;
}

//special function meant for internal use by the timer update. Correctly persists DIV val
void Mmu::SaveDiv(uint8_t val)
{
	Memory[0xFF04] = val;
}
