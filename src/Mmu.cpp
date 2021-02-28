#include "Mmu.h"
#include <iostream>

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

	return Memory[addr]; //just return the mapped memory
}

void Mmu::WriteByte(uint16_t addr, uint8_t val)
{
	//there should be oodles of logic here since a "write" to a location is used to toggle all kinds of things on the gameboy
	
	if (addr == 0xFF01) //TODO: implement serial. Stubbing serial output for now in order to read the results of blargg's test roms
	{
		std::cout << (char)val << std::flush;
	}

	//TODO: this is not even close to right
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
