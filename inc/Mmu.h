#pragma once
#include <stdint.h>
#include <array>
#include <vector>
#include <iostream>
#include "FileOps.h"
#include "Apu.h"

class Mmu
{
public:
	Mmu(Apu* __apu);
	uint8_t		ReadByte(uint16_t addr);
	void		WriteByte(uint16_t addr, uint8_t val);
	uint16_t	ReadWord(uint16_t addr);
	void		WriteWord(uint16_t addr, uint16_t val);
	uint8_t*	GetROM();
	uint8_t*	GetDMGBootRom();
	uint8_t*    GetCGBBootRom();

	void        SetCGBMode(bool enableCGB);
	bool		GetCGBMode();
	bool		GetCGBSupport();

	uint8_t		ReadVRAMDirect(uint16_t addr, uint8_t bank);

	uint32_t	GetBGPColor(uint8_t paletteNum, uint8_t index);
	uint32_t	GetOBPColor(uint8_t paletteNum, uint8_t index);

	void Tick(uint16_t cycles);

	bool isBootRomEnabled();

	bool isDMAInProgress();

	bool isHDMAInProgress();

	void DoHDMATransfer();

	void WriteByteDirect(uint16_t addr, uint8_t val);

	uint8_t	ReadByteDirect(uint16_t addr);

	void ParseRomHeader(const std::string& romFileName);

	void LoadSave(const std::string& romFileName);

	void SaveGame(const std::string& romFileName);

	//void SaveDiv(uint8_t val);
	//void SaveStat(uint8_t val);
	//uint16_t master_clock{ 0x00 };
	uint16_t master_clock{ 0xDC88 }; //TODO: set this to 0 once the bootrom PPU timing is correct
	uint16_t rtc_clock = 0;
	uint32_t rtc_ticks = 0;

	uint8_t currentPPUMode{ 0 };

	struct {
		uint8_t buttons{ 0x0F };
		uint8_t directions{ 0x0F };
		uint8_t down  = 0x08;
		uint8_t up    = 0x04;
		uint8_t left  = 0x02;
		uint8_t right = 0x01;
		uint8_t start_button  = 0x08;
		uint8_t select_button = 0x04;
		uint8_t b_button      = 0x02;
		uint8_t a_button      = 0x01;
	} Joypad;

	uint8_t DMASpeed = 0x01;

	void RegisterApu(Apu* which);

private:
	bool cgbMode = false;
	bool cgbSupport = false;
	bool sgbSupport = false;
	bool bootRomEnabled = true;

	

	bool DMAInProgress = false;
	uint16_t DMACycles = 0x0000;
	uint16_t DMABaseAddr = 0x0000;

	bool HDMAInProgress = false;
	uint16_t HDMATransferredTotal = 0x0000;
	uint8_t HDMATransferredThisLine = 0x00;
	uint16_t HDMALength = 0x0000;
	uint16_t HDMASrcAddr = 0x0000;
	uint16_t HDMADestAddr = 0x0000;

	uint16_t currentRomBank = 1;
	uint16_t totalRomBanks  = 2;
	uint8_t currentRamBank = 1;
	uint8_t totalRamBanks  = 1;
	bool isCartRamEnabled = false;
	bool doesRTCExist = false;
	bool isRTCEnabled = false;
	uint8_t rtcLatchRegs = 0x00; //if switching from 00 to 01, latch/unlatch
	bool isRTCLatched = false;
	
	enum RTCREGS {S = 0, M, H, DL, DH, NONE};

	RTCREGS mappedRTCReg = RTCREGS::NONE;

	uint8_t rtcRegValues[5] = { 0 };
	uint8_t latchedRtcRegValues[5] = { 0 };

	uint8_t DMGBootROM[0x100] = { 0 };
	uint8_t CGBBootROM[0x900] = { 0 };

	uint8_t ROM[0x800000] = { 0 }; //8 MB, the max CGB rom size. Probably excessive but it should always work.

	uint8_t Memory[0x10000] = { 0 }; //the currently active mapped memory

	bool hasSaveBattery = false;

	std::vector<uint8_t> CartRam;

	std::string saveFileName;

	uint8_t ReadCartRam(uint16_t addr);
	void WriteCartRam(uint16_t addr, uint8_t val);

	enum MBC_TYPE { NOMBC, MBC1, MBC2, MBC3, MBC5, UNKNOWN };
	MBC_TYPE currentMBC = MBC_TYPE::NOMBC;

	uint8_t lowBank = 0x01;
	uint8_t hiBank = 0x00;
	uint8_t mbc1Mode = 0;

	void WriteMBC1(uint16_t addr, uint8_t val);
	void WriteMBC2(uint16_t addr, uint8_t val);
	void WriteMBC3(uint16_t addr, uint8_t val);
	void WriteMBC5(uint16_t addr, uint8_t val);


	//ad hoc cgb stuff
	std::array<uint8_t, 64> cgb_BGP;
	std::array<uint8_t, 64> cgb_OBP;

	uint8_t currentVRAMBank = 0;
	uint8_t currentWRAMBank = 1;
	std::array<std::array<uint8_t, 0x2000>, 2> VRAM;
	std::array<std::array<uint8_t, 0x1000>, 8> WRAM;

	Apu* apu = nullptr;
};

