#pragma once
#include "Mmu.h"
class Ppu
{
public:
	Ppu(Mmu* __mmu);
	void Tick(uint16_t cycles);
private:
	Mmu* mmu;
	uint64_t PpuCycles{ 0 };
};

