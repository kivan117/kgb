#pragma once
#include <stdint.h>

class Mmu
{
public:
	Mmu();
	uint8_t		ReadByte(uint16_t addr);
	void		WriteByte(uint16_t addr, uint8_t val);
	uint16_t	ReadWord(uint16_t addr);
	void		WriteWord(uint16_t addr, uint16_t val);
	uint8_t*	GetROM();
	uint8_t*	GetDMGBootRom();

	bool isBootRomEnabled();

	void WriteByteDirect(uint16_t addr, uint8_t val);

	//void SaveDiv(uint8_t val);
	//void SaveStat(uint8_t val);
	//uint16_t master_clock{ 0x00 };
	uint16_t master_clock{ 0xDC88 }; //TODO: set this to 0 once the bootrom PPU timing is correct

	uint8_t currentPPUMode{ 0 };

private:
	bool bootRomEnabled = true;

	uint8_t currentRomBank = 1;

	uint8_t DMGBootROM[0x100] = { 0 };

	uint8_t ROM[0x800000] = { 0 }; //8 MB, the max CGB rom size. Probably excessive but it should always work.

	uint8_t Memory[0x10000] = { 0 }; //the currently active mapped memory
};

