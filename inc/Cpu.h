#pragma once
#include "Mmu.h"
#include "Ppu.h"
#include <stdint.h>
class Cpu
{
public:
	Cpu(Mmu* __mmu, Ppu* __ppu);
	void Tick();
	bool GetStopped();
	uint64_t GetTotalCycles();
	void ResetTotalCycles();
private:
	Mmu* mmu;
	Ppu* ppu;

	void Execute(uint8_t op);

	void PrintCPUState();

	//struct {
	//	uint8_t F{ 0 };
	//	uint8_t A{ 0 };
	//	uint8_t C{ 0 };
	//	uint8_t B{ 0 };
	//	uint8_t E{ 0 };
	//	uint8_t D{ 0 };
	//	uint8_t L{ 0 };
	//	uint8_t H{ 0 };
	//} Regs;

	//uint16_t& AF = *((uint16_t*) &(Regs.F));
	//uint16_t& BC = *((uint16_t*) &(Regs.C));
	//uint16_t& DE = *((uint16_t*) &(Regs.E));
	//uint16_t& HL = *((uint16_t*) &(Regs.L));

	struct {
		union {
			struct {
				uint8_t F;
				uint8_t A;
			};
			uint16_t AF;
		};
		union {
			struct {
				uint8_t C;
				uint8_t B;
			};
			uint16_t BC;
		};
		union {
			struct {
				uint8_t E;
				uint8_t D;
			};
			uint16_t DE;
		};
		union {
			struct {
				uint8_t L;
				uint8_t H;
			};
			uint16_t HL;
		};
	} Regs;

	uint16_t SP{ 0 };
	uint16_t PC{ 0 };

	void Push(uint16_t addr);
	uint16_t Pop();

	struct {
		uint8_t zero{ 0 };
		uint8_t negative{ 0 };
		uint8_t halfcarry{ 0 };
		uint8_t carry{ 0 };
	} flags;

	void SetZero(int newVal);
	void SetNeg(int newVal);
	void SetHalfCarry(int newVal);
	void SetCarry(int newVal);

	enum CARRYMODE {NONE=0, CARRY=1, HALFCARRY=2, BOTH=3};

	void SetFlags(uint8_t OpA, uint8_t OpB, uint8_t inputCarryBit, bool subtraction, CARRYMODE carryMode);

	void SyncFlagsFromReg();

	void CalcCarry(uint8_t OpA, uint8_t OpB, uint8_t inputCarryBit, bool subtraction, CARRYMODE carryMode);

	void CalcCarry16(uint16_t OpA, uint16_t OpB, uint8_t inputCarryBit, bool subtraction, CARRYMODE carryMode);

	void TestBit(uint8_t Op, uint8_t bitNum);

	uint8_t SetBit(uint8_t Op, uint8_t bitNum, bool enabled);

	bool Halted{ false };
	bool Stopped{ false };
	bool InterruptsEnabled{ true }; // IME flag. Not mapped to memory
	bool EI_DelayedInterruptEnableFlag = false; //set by EI, will be checked to turn InterruptsEnabled on one instruction later

	void HandleInterrupts();


	uint64_t CycleCounter{ 0 };
	uint64_t TotalCyclesCounter{ 0 };
	uint64_t OpsCounter{ 0 };

	uint64_t div_cycles{ 0 };
	uint64_t timer_cycles{ 0 };
	const uint16_t timer_cycle_thresholds[4] = { 1024, 16, 64, 256 };

	void UpdateTimers(uint16_t cycles);

	//the minimum amount of t-cycles each operation takes. 
	//for variable-time ops, the extra time needs to be added by the op
	const uint8_t CyclesPerOp[256] = {
	 4, 12,  8,  8,  4,  4,  8,  4, 20,  8,  8,  8,  4,  4,  8,  4,
	 4,	12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4,
	 8, 12,  8,  8,  4,  4,  8,  4,  8,  8,  8,  8,  4,  4,  8,  4,
	 8, 12,  8,  8, 12, 12, 12,  4,  8,  8,  8,  8,  4,  4,  8,  4,
	 4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
	 4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
	 4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
	 8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4,
	 4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
	 4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
	 4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
	 4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
	 8, 12, 12, 16, 12, 16,  8, 16,  8, 16, 12,  0, 12, 24,  8, 16,
	 8, 12, 12,  0, 12, 16,  8, 16,  8, 16, 12,  0, 12,  0,  8, 16,
	12,	12,  8,  0,  0, 16,  8, 16, 16,  4, 16,  0,  0,  0,  8, 16,
	12, 12,  8,  4,  0, 16,  8, 16, 12,  8, 16,  4,  0,  0,  8, 16, 
	};

	const uint8_t CyclesPerOpCB[256] = {
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
	 8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
	 8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
	 8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	 8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
	};

	void UpdatePpu();

};

