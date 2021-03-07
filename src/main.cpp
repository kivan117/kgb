#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cassert>
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

	while (!cpu->GetStopped())
	{
		cpu->Tick();
	}

	return 0;
}