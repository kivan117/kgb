#include "Ppu.h"

Ppu::Ppu(Mmu* __mmu) : mmu(__mmu)
{

}

void Ppu::Tick(uint16_t cycles)
{
	PpuCycles += cycles;
}