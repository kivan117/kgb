#include "Cpu.h"
#include <cassert>
#include <iostream>
#include <iomanip>

#include <fstream>



void audio_callback(void* user, Uint8* stream, int len) {
	Cpu* cpu = (Cpu*)user;
	if (!cpu)
	{
		for (int i = 0; i < len; i++)
		{
			stream[i] = 0;
		}
		return;
	}
	if (!cpu->apu)
	{
		for (int i = 0; i < len; i++)
		{
			stream[i] = 0;
		}
		return;
	}
	cpu->audio_frames_requested = len;
	SDL_AudioStreamGet(cpu->apu->audio_stream, stream, len); //copy audio buffer to audio output
	if (std::floor(cpu->GetThrottle()) > 1)
	{
		uint8_t* trash = new uint8_t[len];

		for (int n = 0; n < std::floor(cpu->GetThrottle()) - 1; n++)
			SDL_AudioStreamGet(cpu->apu->audio_stream, trash, len);

		delete[] trash;
	}
}

Cpu::Cpu(Mmu* __mmu, Ppu* __ppu, Apu* __apu) : mmu(__mmu), ppu(__ppu), apu(__apu)
{
	if (apu) {
		SDL_zero(audio_spec);
		audio_spec.freq = 48000;
		audio_spec.format = AUDIO_S16;
		audio_spec.channels = 2;
		audio_spec.samples = 1024;
		audio_spec.userdata = this;
		audio_spec.callback = audio_callback;
		audio_device = SDL_OpenAudioDevice(NULL, 0, &audio_spec, NULL, 0);
		SDL_PauseAudioDevice(audio_device, 0);
	}

	if (!mmu->isBootRomEnabled()) //fake it til you make it
	{
		if (!mmu->GetCGBMode())
		{
			//DMG
			Regs.AF = 0x01B0;
			Regs.BC = 0x0013;
			Regs.DE = 0x00D8;
			Regs.HL = 0x014D;
			SP = 0xFFFE;
			PC = 0x0100;
			UpdateTimers(0xABCC); //set DIV
		}
		else 
		{
			if (mmu->GetCGBSupport())
			{
				//CGB in CGB Mode
				Regs.AF = 0x1180;
				Regs.BC = 0x0000;
				Regs.DE = 0xFF56;
				Regs.HL = 0x000D;
				SP = 0xFFFE;
				PC = 0x0100;
				UpdateTimers(0x1EA0); //set DIV
			}
			else
			{
				//CGB in DMG mode
				Regs.AF = 0x0180;
				Regs.BC = 0x0000;
				Regs.DE = 0x0008;
				Regs.HL = 0x007C;
				SP = 0xFFFE;
				PC = 0x0100;
				UpdateTimers(0x267C); //set DIV
			}
		}

		SyncFlagsFromReg();

		//set mmio registers
		//mmu->WriteByteDirect(0xFF44, 0x90); //stub LY to 0x90 (line 144, begin VBlank)
		mmu->WriteByteDirect(0xFF00, 0xFF); //stub input, no buttons pressed

		mmu->WriteByteDirect(0xFF05, 0x00);// TIMA
		mmu->WriteByteDirect(0xFF06, 0x00);// TMA
		mmu->WriteByteDirect(0xFF07, 0x00);// TAC
		mmu->WriteByteDirect(0xFF0F, 0x00);// IF
		mmu->WriteByteDirect(0xFF10, 0x80);// NR10
		mmu->WriteByteDirect(0xFF11, 0xBF);// NR11
		mmu->WriteByteDirect(0xFF12, 0xF3);// NR12
		mmu->WriteByteDirect(0xFF14, 0xBF);// NR14
		mmu->WriteByteDirect(0xFF16, 0x3F);// NR21
		mmu->WriteByteDirect(0xFF17, 0x00);// NR22
		mmu->WriteByteDirect(0xFF19, 0xBF);// NR24
		mmu->WriteByteDirect(0xFF1A, 0x7F);// NR30
		mmu->WriteByteDirect(0xFF1B, 0xFF);// NR31
		mmu->WriteByteDirect(0xFF1C, 0x9F);// NR32
		mmu->WriteByteDirect(0xFF1E, 0xBF);// NR34
		mmu->WriteByteDirect(0xFF20, 0xFF);// NR41
		mmu->WriteByteDirect(0xFF21, 0x00);// NR42
		mmu->WriteByteDirect(0xFF22, 0x00);// NR43
		mmu->WriteByteDirect(0xFF23, 0xBF);// NR44
		mmu->WriteByteDirect(0xFF24, 0x77);// NR50
		mmu->WriteByteDirect(0xFF25, 0xF3);// NR51
		mmu->WriteByteDirect(0xFF26, 0xF1);// -GB, $F0 - SGB; NR52
		mmu->WriteByteDirect(0xFF40, 0x91);// LCDC
		mmu->WriteByteDirect(0xFF42, 0x00);// SCY
		mmu->WriteByteDirect(0xFF43, 0x00);// SCX
		mmu->WriteByteDirect(0xFF45, 0x00);// LYC
		mmu->WriteByteDirect(0xFF47, 0xFC);// BGP
		mmu->WriteByteDirect(0xFF48, 0xFF);// OBP0
		mmu->WriteByteDirect(0xFF49, 0xFF);// OBP1
		mmu->WriteByteDirect(0xFF4A, 0x00);// WY
		mmu->WriteByteDirect(0xFF4B, 0x00);// WX
		mmu->WriteByteDirect(0xFFFF, 0x00);// IE
		mmu->WriteByteDirect(0xFF50, 0x01); //disable boot rom
		
	}
	mmu->WriteByteDirect(0xFF4D, 0x7E);// Speed Switch
}

void Cpu::Tick()
{

	if (Stopped)
	{
		uint8_t speedReg = mmu->ReadByteDirect(0xFF4D);
		if (speedReg & 1)
		{
			isDoubleSpeedEnabled = !isDoubleSpeedEnabled;
			if (isDoubleSpeedEnabled)
			{
				mmu->WriteByteDirect(0xFF4D, 0x80);
				mmu->DMASpeed = 0x02;
			}
			else
			{
				mmu->WriteByteDirect(0xFF4D, 0x7E);
				mmu->DMASpeed = 0x01;
			}
			//CycleCounter = 8200;
		}
		PC++;
		Stopped = false;
		return;
	}

	if (Halted)
	{
		OpsCounter++;
		CycleCounter += 4;
	}
	else
	{
		uint8_t nextOp = mmu->ReadByte(PC);
		PC += 1;
		Execute(nextOp);
	}

	//PrintCPUState();
	UpdatePpu();
	UpdateMmu();
	//handle interrupts
	HandleInterrupts();
	UpdateTimers(CycleCounter);
	CycleCounter = 0;
}

void Cpu::Push(uint16_t addr)
{
	SP -= 2;
	mmu->WriteWord(SP, addr);
}

uint16_t Cpu::Pop()
{
	uint16_t addr = mmu->ReadWord(SP);
	SP += 2;
	return addr;
}

bool Cpu::GetStopped()
{
	return Stopped;
}

uint64_t Cpu::GetTotalCycles()
{
	return TotalCyclesCounter;
}

void Cpu::SetTotalCycles(uint64_t val)
{
	TotalCyclesCounter = val;
	//if(isDoubleSpeedEnabled)
	//	TotalCyclesCounter -= (456 * 154) * 2;
	//else
	//	TotalCyclesCounter -= (456*154);
}

uint64_t Cpu::GetFrameCycles()
{
	return FrameCyclesCounter;
}

void Cpu::SetFrameCycles(uint64_t val)
{
	FrameCyclesCounter = val;
}

bool Cpu::GetDoubleSpeedMode()
{
	return isDoubleSpeedEnabled;
}

void Cpu::SetThrottle(double setpoint)
{
	throttle = setpoint;
}

double Cpu::GetThrottle()
{
	return throttle;
}

void Cpu::SetZero(int newVal)
{
	assert((newVal == 1) || (newVal == 0)); //don't allow nonsense values
	flags.zero = newVal;
	Regs.F &= 0x70; // Mask with 0111 0000
	Regs.F |= (newVal << 7); //OR with new value at Z flag bit position
}

void Cpu::SetNeg(int newVal)
{
	assert((newVal == 1) || (newVal == 0)); //don't allow nonsense values
	flags.negative = newVal;
	Regs.F &= 0xB0; // Mask with 1011 0000
	Regs.F |= (newVal << 6); //OR with new value at N flag bit position
}

void Cpu::SetHalfCarry(int newVal)
{
	assert((newVal == 1) || (newVal == 0)); //don't allow nonsense values
	flags.halfcarry = newVal;
	Regs.F &= 0xD0; // Mask with 1101 0000
	Regs.F |= (newVal << 5); //OR with new value at H flag bit position
}

void Cpu::SetCarry(int newVal)
{
	assert((newVal == 1) || (newVal == 0)); //don't allow nonsense values
	flags.carry = newVal;
	Regs.F &= 0xE0; // Mask with 1110 0000
	Regs.F |= (newVal << 4); //OR with new value at C flag bit position
}

void Cpu::CalcCarry(uint8_t OpA, uint8_t OpB, uint8_t inputCarryBit, bool subtraction, CARRYMODE carryMode)
{
	if (carryMode == CARRYMODE::HALFCARRY || carryMode == CARRYMODE::BOTH) //set half carry
	{
		uint32_t tempSum = OpA + (subtraction ? ~OpB : OpB) + (subtraction ? (!(inputCarryBit)) : inputCarryBit);

		uint8_t newHC = ((tempSum ^ OpA ^ OpB) >> 4) & 1; //some stack overflow magic for calculating carry bits

		SetHalfCarry(newHC);
	}

	if (carryMode == CARRYMODE::CARRY || carryMode == CARRYMODE::BOTH) //set carry
	{
		uint32_t tempSum = OpA + (subtraction ? ~OpB : OpB) + (subtraction ? (!(inputCarryBit)) : inputCarryBit);

		uint8_t newC = ((tempSum ^ OpA ^ OpB) >> 8) & 1; //some stack overflow magic for calculating carry bits

		SetCarry(newC);
	}
}

void Cpu::CalcCarry16(uint16_t OpA, uint16_t OpB, uint8_t inputCarryBit, bool subtraction, CARRYMODE carryMode)
{
	if (carryMode == CARRYMODE::HALFCARRY || carryMode == CARRYMODE::BOTH) //set half carry
	{
		uint32_t tempSum = OpA + (subtraction ? ~OpB : OpB) + (subtraction ? (!(inputCarryBit)) : inputCarryBit);

		uint8_t newHC = ((tempSum ^ OpA ^ OpB) >> 12) & 1; //some stack overflow magic for calculating carry bits

		SetHalfCarry(newHC);
	}

	if (carryMode == CARRYMODE::CARRY || carryMode == CARRYMODE::BOTH) //set carry
	{
		uint32_t tempSum = OpA + (subtraction ? ~OpB : OpB) + (subtraction ? (!(inputCarryBit)) : inputCarryBit);

		uint8_t newC = ((tempSum ^ OpA ^ OpB) >> 16) & 1; //some stack overflow magic for calculating carry bits

		SetCarry(newC);
	}
}

void Cpu::SetFlags(uint8_t OpA, uint8_t OpB, uint8_t inputCarryBit, bool subtraction, CARRYMODE carryMode)
{
	uint8_t answer;
	if (!subtraction)
		answer = OpA + OpB + inputCarryBit;
	else
		answer = OpA + ~OpB + (!inputCarryBit);
	SetZero((int)(answer == 0));
	SetNeg((int)subtraction);

	CalcCarry(OpA, OpB, inputCarryBit, subtraction, carryMode);

}

void Cpu::SyncFlagsFromReg()
{
	flags.zero = ((Regs.F & 0x80) >> 7);
	flags.negative = ((Regs.F & 0x40) >> 6);
	flags.halfcarry = ((Regs.F & 0x20) >> 5);
	flags.carry = ((Regs.F & 0x10) >> 4);
}

void Cpu::TestBit(uint8_t Op, uint8_t bitNum)
{
	SetZero((Op & (1 << bitNum)) == 0);
	SetNeg(0);
	SetHalfCarry(1);
}

uint8_t Cpu::SetBit(uint8_t Op, uint8_t bitNum, bool enabled)
{
	if (enabled)
		return (Op | (1 << bitNum));

	uint8_t temp = 1 << bitNum;
	return Op & ~temp;
}

void Cpu::HandleInterrupts()
{
	//FFFF - IE - interrupt enabled
	//FF0F - IF - interrupt requested

	//Bit 0: V-Blank  0x40
	//Bit 1: LCD STAT 0x48
	//Bit 2: Timer    0x50
	//Bit 3: Serial   0x58
	//Bit 4: Joypad   0x60

	uint8_t REG_IE = mmu->ReadByteDirect(0xFFFF);
	uint8_t REG_IF = mmu->ReadByteDirect(0xFF0F);

	//at least 1 interrupt is both enabled and requested
	if (REG_IE & REG_IF & 0x1F)
	{
		//even if IME is disabled, any interrupt that's enabled and requested will clear Halt status
		if (Halted)
		{
			Halted = false;
			CycleCounter += 4;

			//TODO: implement halt bug here?
		}

		if (InterruptsEnabled) // IME flag is true, disable the IF bit and jump to the handler for the highest priority interrupt that's enabled AND requested
		{
			Push(PC);
			InterruptsEnabled = false;
			CycleCounter += 20; //assuming interrupt handling process takes 20 cycles per z80 spec sheet / pan docs

			if (REG_IE & REG_IF & 0x1) //v-blank
			{
				mmu->WriteByte(0xFF0F, REG_IF & (0xFE)); // disable vblank bit in IF.   IF & 1111 1110
				PC = 0x0040;
			}
			else if (REG_IE & REG_IF & 0x2) //lcd stat
			{
				mmu->WriteByte(0xFF0F, REG_IF & (0xFD)); // disable lcd stat bit in IF. IF & 1111 1101
				PC = 0x0048;
			}
			else if (REG_IE & REG_IF & 0x4) //timer
			{
				mmu->WriteByte(0xFF0F, REG_IF & (0xFB)); // disable timer bit in IF.    IF & 1111 1011
				PC = 0x0050;
			}
			else if (REG_IE & REG_IF & 0x8) //serial
			{
				mmu->WriteByte(0xFF0F, REG_IF & (0xF7)); // disable serial bit in IF.   IF & 1111 0111
				PC = 0x0058;
			}
			else if (REG_IE & REG_IF & 0x10) //joypad
			{
				mmu->WriteByte(0xFF0F, REG_IF & (0xEF)); // disable joypad bit in IF.   IF & 1110 1111
				PC = 0x0060;
			}
		}
	}
	return;
}

void Cpu::UpdateTimers(uint16_t cycles)
{
	mmu->master_clock += cycles;

	if (!Stopped) //update DIV
	{
		//div_cycles += cycles;
		//uint8_t div = mmu->ReadByte(0xFF04);

		//while (div_cycles >= 256)
		//{
		//	div++;
		//	div_cycles -= 256;
		//}
		uint8_t div = mmu->master_clock >> 8;
		mmu->WriteByteDirect(0xFF04, div);

	}

	
	uint8_t tima = mmu->ReadByteDirect(0xFF05);
	uint8_t tma  = mmu->ReadByteDirect(0xFF06);
	uint8_t tac  = mmu->ReadByteDirect(0xFF07);
	

	if (tac & 0x04) // tac & 0000 0100, timer enable bit
	{
		timer_cycles += cycles;
		uint16_t cycle_threshold = timer_cycle_thresholds[tac & 0x03]; // frequency set by bottom 2 bits of tac. tac & 0000 0011
		while (timer_cycles >= cycle_threshold)
		{
			if (tima == 0xFF) // increasing the timer would overflow
			{
				//set timer counter to the timer modulo value
				tima = tma;
				mmu->WriteByte(0xFF05, tima);

				//request timer interrupt by setting bit 2 of 0xFF0F
				uint8_t reg_if = mmu->ReadByte(0xFF0F);
				reg_if |= 0x04;
				mmu->WriteByte(0xFF0F, reg_if);
			}
			else
				tima++;

			timer_cycles -= cycle_threshold;

		}

		mmu->WriteByte(0xFF05, tima);

	}

	TotalCyclesCounter += cycles;
	FrameCyclesCounter += cycles;

}

void Cpu::UpdatePpu()
{
	ppu->Tick(CycleCounter);
}

void Cpu::UpdateMmu()
{
	mmu->Tick(CycleCounter);
}

void Cpu::Execute(uint8_t op)
{
	OpsCounter++;
	CycleCounter = CyclesPerOp[op];

	//EI enables interrupts one instruction late
	InterruptsEnabled = (InterruptsEnabled || EI_DelayedInterruptEnableFlag);
	EI_DelayedInterruptEnableFlag = false;

	//temporary immediate values
	uint8_t u8iv;
	int8_t i8iv;
	uint16_t u16iv;
	//int16_t i16iv;

	switch (op)
	{
	//Misc
	case(0x00): break; //NOP
	case(0x10): Stopped = true; break;
	case(0x76): Halted = true; break; //HALT
	case(0xF3): InterruptsEnabled = false; break; //DI
	case(0xFB): EI_DelayedInterruptEnableFlag = true;  break; //EI

	//Rotate/Shift/Bitops

	case(0x07): u8iv = Regs.A >> 7; Regs.A = Regs.A << 1; Regs.A |= u8iv; SetZero(0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLCA
	case(0x17): u8iv = Regs.A >> 7; Regs.A = Regs.A << 1; Regs.A |= (flags.carry ? 1 : 0); SetZero(0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLA
	case(0x0F): u8iv = Regs.A & 1; Regs.A = Regs.A >> 1; Regs.A |= ((u8iv ? 1 : 0) << 7);        SetZero(0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRCA
	case(0x1F): u8iv = Regs.A & 1; Regs.A = Regs.A >> 1; Regs.A |= ((flags.carry ? 1 : 0) << 7); SetZero(0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRA

	case(0x37): SetNeg(0); SetHalfCarry(0); SetCarry(1); break; //SCF
	case(0x3F): SetNeg(0); SetHalfCarry(0); SetCarry(!flags.carry); break; //CCF
	case(0x27): { //DAA
		if (flags.negative)
		{
			if (flags.carry)
			{
				Regs.A -= 0x60;
				SetCarry(1);
			}
			if (flags.halfcarry)
			{
				Regs.A -= 0x06;
			}
		}
		else
		{
			if (flags.carry || Regs.A > 0x99)
			{
				Regs.A += 0x60;
				SetCarry(1);
			}
			if (flags.halfcarry || ((Regs.A & 0x0F) > 0x09))
			{
				Regs.A += 0x06;
			}
		}
		SetZero(Regs.A == 0);
		SetHalfCarry(0);
		break;
	}
	case(0x2F): Regs.A = ~Regs.A; SetNeg(1); SetHalfCarry(1); break; // CPL
		
	//Load/Store/Move 8-bit
	case(0x02): mmu->WriteByte(Regs.BC, Regs.A); break; //LD (BC), A
	case(0x06): Regs.B = mmu->ReadByte(PC); PC += 1; break; //LD B, u8
	case(0x0A): Regs.A = mmu->ReadByte(Regs.BC); break; //LD A, (BC)
	case(0x0E): Regs.C = mmu->ReadByte(PC); PC += 1; break; //LD C, u8
	
	case(0x12): mmu->WriteByte(Regs.DE, Regs.A); break; //LD (DE), A
	case(0x16): Regs.D = mmu->ReadByte(PC); PC += 1; break; // LD D, u8
	case(0x1A): Regs.A = mmu->ReadByte(Regs.DE); break; //LD A, (DE)
	case(0x1E): Regs.E = mmu->ReadByte(PC); PC += 1; break; // LD E, u8
	
	case(0x22): mmu->WriteByte(Regs.HL++, Regs.A); break; // LD (HL+), A
	case(0x26): Regs.H = mmu->ReadByte(PC); PC += 1; break; //LD H, u8
	case(0x2A): Regs.A = mmu->ReadByte(Regs.HL++); break; //LD A, (HL+)
	case(0x2E): Regs.L = mmu->ReadByte(PC); PC += 1; break; //LD L, u8
	
	case(0x32): mmu->WriteByte(Regs.HL--, Regs.A); break; // LD (HL-), A
	case(0x36): mmu->WriteByte(Regs.HL, mmu->ReadByte(PC++)); break; //LD (HL), u8
	case(0x3A): Regs.A = mmu->ReadByte(Regs.HL--); break; //LD A, (HL-)
	case(0x3E): Regs.A = mmu->ReadByte(PC); PC += 1; break; //LD A, u8
	
	case(0x40): Regs.B = Regs.B; break; //LD B, B
	case(0x41): Regs.B = Regs.C; break; //LD B, C
	case(0x42): Regs.B = Regs.D; break; //LD B, D
	case(0x43): Regs.B = Regs.E; break; //LD B, E
	case(0x44): Regs.B = Regs.H; break; //LD B, H
	case(0x45): Regs.B = Regs.L; break; //LD B, L
	case(0x46): Regs.B = mmu->ReadByte(Regs.HL); break; // LD B, (HL)
	case(0x47): Regs.B = Regs.A; break; //LD B, A
	
	case(0x48): Regs.C = Regs.B; break; //LD C, B
	case(0x49): Regs.C = Regs.C; break; //LD C, C
	case(0x4A): Regs.C = Regs.D; break; //LD C, D
	case(0x4B): Regs.C = Regs.E; break; //LD C, E
	case(0x4C): Regs.C = Regs.H; break; //LD C, H
	case(0x4D): Regs.C = Regs.L; break; //LD C, L
	case(0x4E): Regs.C = mmu->ReadByte(Regs.HL); break; // LD C, (HL)
	case(0x4F): Regs.C = Regs.A; break; //LD C, A

	case(0x50): Regs.D = Regs.B; break; //LD D, B
	case(0x51): Regs.D = Regs.C; break; //LD D, C
	case(0x52): Regs.D = Regs.D; break; //LD D, D
	case(0x53): Regs.D = Regs.E; break; //LD D, E
	case(0x54): Regs.D = Regs.H; break; //LD D, H
	case(0x55): Regs.D = Regs.L; break; //LD D, L
	case(0x56): Regs.D = mmu->ReadByte(Regs.HL); break; // LD D, (HL)
	case(0x57): Regs.D = Regs.A; break; //LD D, A

	case(0x58): Regs.E = Regs.B; break; //LD E, B
	case(0x59): Regs.E = Regs.C; break; //LD E, C
	case(0x5A): Regs.E = Regs.D; break; //LD E, D
	case(0x5B): Regs.E = Regs.E; break; //LD E, E
	case(0x5C): Regs.E = Regs.H; break; //LD E, H
	case(0x5D): Regs.E = Regs.L; break; //LD E, L
	case(0x5E): Regs.E = mmu->ReadByte(Regs.HL); break; // LD E, (HL)
	case(0x5F): Regs.E = Regs.A; break; //LD E, A
	
	case(0x60): Regs.H = Regs.B; break; //LD H, B
	case(0x61): Regs.H = Regs.C; break; //LD H, C
	case(0x62): Regs.H = Regs.D; break; //LD H, D
	case(0x63): Regs.H = Regs.E; break; //LD H, E
	case(0x64): Regs.H = Regs.H; break; //LD H, H
	case(0x65): Regs.H = Regs.L; break; //LD H, L
	case(0x66): Regs.H = mmu->ReadByte(Regs.HL); break; // LD H, (HL)
	case(0x67): Regs.H = Regs.A; break; //LD D, A

	case(0x68): Regs.L = Regs.B; break; //LD L, B
	case(0x69): Regs.L = Regs.C; break; //LD L, C
	case(0x6A): Regs.L = Regs.D; break; //LD L, D
	case(0x6B): Regs.L = Regs.E; break; //LD L, E
	case(0x6C): Regs.L = Regs.H; break; //LD L, H
	case(0x6D): Regs.L = Regs.L; break; //LD L, L
	case(0x6E): Regs.L = mmu->ReadByte(Regs.HL); break; // LD L, (HL)
	case(0x6F): Regs.L = Regs.A; break; //LD L, A
	
	case(0x70): mmu->WriteByte(Regs.HL, Regs.B); break; // LD (HL), B
	case(0x71): mmu->WriteByte(Regs.HL, Regs.C); break; // LD (HL), C
	case(0x72): mmu->WriteByte(Regs.HL, Regs.D); break; // LD (HL), D
	case(0x73): mmu->WriteByte(Regs.HL, Regs.E); break; // LD (HL), E
	case(0x74): mmu->WriteByte(Regs.HL, Regs.H); break; // LD (HL), H
	case(0x75): mmu->WriteByte(Regs.HL, Regs.L); break; // LD (HL), L
	//   0x76   HALT
	case(0x77): mmu->WriteByte(Regs.HL, Regs.A); break; // LD (HL), A
	
	case(0x78): Regs.A = Regs.B; break; //LD A, B
	case(0x79): Regs.A = Regs.C; break; //LD A, C
	case(0x7A): Regs.A = Regs.D; break; //LD A, D
	case(0x7B): Regs.A = Regs.E; break; //LD A, E
	case(0x7C): Regs.A = Regs.H; break; //LD A, H
	case(0x7D): Regs.A = Regs.L; break; //LD A, L
	case(0x7E): Regs.A = mmu->ReadByte(Regs.HL); break; // LD A, (HL)
	case(0x7F): Regs.A = Regs.A; break; //LD A, A
	
	case(0xE0): mmu->WriteByte(0xFF00 + mmu->ReadByte(PC++), Regs.A); break; //LD (FF00+u8), A
	case(0xE2): mmu->WriteByte(0xFF00 + Regs.C, Regs.A); break; //LD (FF00+C), A
	case(0xEA): mmu->WriteByte(mmu->ReadWord(PC), Regs.A); PC += 2; break; //LD (u16), A
	
	case(0xF0): u8iv = mmu->ReadByte(PC); PC += 1; Regs.A = mmu->ReadByte(0xFF00 + u8iv); break; //LD A, (FF00+u8)
	case(0xF2): Regs.A = mmu->ReadByte(0xFF00 + Regs.C); break; //LD A, (FF00+C)
	case(0xFA): u16iv = mmu->ReadWord(PC); PC += 2; Regs.A = mmu->ReadByte(u16iv); break; // LD A, (u16)

	//Load/Store/Move 16-bit
	case(0x01): Regs.BC = mmu->ReadWord(PC); PC += 2; break; //LD BC, u16
	case(0x11): Regs.DE = mmu->ReadWord(PC); PC += 2; break; //LD DE, u16
	case(0x21): Regs.HL = mmu->ReadWord(PC); PC += 2; break; //LD HL, u16
	case(0x31): SP = mmu->ReadWord(PC); PC += 2; break; //LD SP, u16
	case(0xC1): Regs.BC = Pop(); break; //Pop  BC
	case(0xC5): Push(Regs.BC); break;   //Push BC	
	case(0xD1): Regs.DE = Pop(); break; //Pop  DE
	case(0xD5): Push(Regs.DE); break;   //Push DE
	case(0xE1): Regs.HL = Pop(); break; //Pop  HL
	case(0xE5): Push(Regs.HL); break;   //Push HL
	case(0xF1): Regs.AF = (Pop() & 0xFFF0); SyncFlagsFromReg(); break; //Pop  AF
	case(0xF5): Push(Regs.AF); break;   //Push AF

	case(0x08): mmu->WriteWord(mmu->ReadWord(PC), SP); PC += 2; break; //LD (u16), SP

	case(0xF8): i8iv = mmu->ReadByte(PC++); CalcCarry(SP, (uint16_t)i8iv, 0, false, CARRYMODE::BOTH); SetZero(0); SetNeg(0); Regs.HL = SP + i8iv; break; // LD HL, SP+i8

	case(0xF9): SP = Regs.HL; break; //LD SP, HL

	//ALU 8-bit
	case(0x04): SetFlags(Regs.B, 1, 0, false, CARRYMODE::HALFCARRY); Regs.B++; break; //INC B
	case(0x14): SetFlags(Regs.D, 1, 0, false, CARRYMODE::HALFCARRY); Regs.D++; break; //INC D
	case(0x24): SetFlags(Regs.H, 1, 0, false, CARRYMODE::HALFCARRY); Regs.H++; break; //INC H
	case(0x34): SetFlags(mmu->ReadByte(Regs.HL), 1, 0, false, CARRYMODE::HALFCARRY);  mmu->WriteByte(Regs.HL, mmu->ReadByte(Regs.HL) + 1); break; //INC (HL)

	case(0x05): SetFlags(Regs.B, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.B--; break; //DEC B
	case(0x15): SetFlags(Regs.D, 1, 0, true, CARRYMODE::HALFCARRY); Regs.D--; break; //DEC D
	case(0x25): SetFlags(Regs.H, 1, 0, true, CARRYMODE::HALFCARRY); Regs.H--; break; //DEC H
	case(0x35): SetFlags(mmu->ReadByte(Regs.HL), 1, 0, true, CARRYMODE::HALFCARRY);  mmu->WriteByte(Regs.HL, mmu->ReadByte(Regs.HL) - 1); break; //DEC (HL)
	
	case(0x0C): SetFlags(Regs.C, 1, 0, false, CARRYMODE::HALFCARRY); Regs.C++; break; //INC C
	case(0x1C): SetFlags(Regs.E, 1, 0, false, CARRYMODE::HALFCARRY); Regs.E++; break; //INC E
	case(0x2C): SetFlags(Regs.L, 1, 0, false, CARRYMODE::HALFCARRY); Regs.L++; break; //INC L
	case(0x3C): SetFlags(Regs.A, 1, 0, false, CARRYMODE::HALFCARRY); Regs.A++; break; //INC A

	case(0x0D): SetFlags(Regs.C, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.C--; break; //DEC C
	case(0x1D): SetFlags(Regs.E, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.E--; break; //DEC E
	case(0x2D): SetFlags(Regs.L, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.L--; break; //DEC L
	case(0x3D): SetFlags(Regs.A, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.A--; break; //DEC A

	case(0x80): SetFlags(Regs.A, Regs.B, 0, false, CARRYMODE::BOTH); Regs.A += Regs.B; break; //ADD A, B
	case(0x81): SetFlags(Regs.A, Regs.C, 0, false, CARRYMODE::BOTH); Regs.A += Regs.C; break; //ADD A, C
	case(0x82): SetFlags(Regs.A, Regs.D, 0, false, CARRYMODE::BOTH); Regs.A += Regs.D; break; //ADD A, D
	case(0x83): SetFlags(Regs.A, Regs.E, 0, false, CARRYMODE::BOTH); Regs.A += Regs.E; break; //ADD A, E
	case(0x84): SetFlags(Regs.A, Regs.H, 0, false, CARRYMODE::BOTH); Regs.A += Regs.H; break; //ADD A, H
	case(0x85): SetFlags(Regs.A, Regs.L, 0, false, CARRYMODE::BOTH); Regs.A += Regs.L; break; //ADD A, L
	case(0x86): u8iv = mmu->ReadByte(Regs.HL); SetFlags(Regs.A, u8iv, 0, false, CARRYMODE::BOTH); Regs.A += u8iv; break; //ADD A, (HL)
	case(0x87): SetFlags(Regs.A, Regs.A, 0, false, CARRYMODE::BOTH); Regs.A += Regs.A; break; //ADD A, A

	case(0x88): u8iv = flags.carry; SetFlags(Regs.A, Regs.B, u8iv, false, CARRYMODE::BOTH); Regs.A += Regs.B + u8iv; break; //ADC A, B
	case(0x89): u8iv = flags.carry; SetFlags(Regs.A, Regs.C, u8iv, false, CARRYMODE::BOTH); Regs.A += Regs.C + u8iv; break; //ADC A, C
	case(0x8A): u8iv = flags.carry; SetFlags(Regs.A, Regs.D, u8iv, false, CARRYMODE::BOTH); Regs.A += Regs.D + u8iv; break; //ADC A, D
	case(0x8B): u8iv = flags.carry; SetFlags(Regs.A, Regs.E, u8iv, false, CARRYMODE::BOTH); Regs.A += Regs.E + u8iv; break; //ADC A, E
	case(0x8C): u8iv = flags.carry; SetFlags(Regs.A, Regs.H, u8iv, false, CARRYMODE::BOTH); Regs.A += Regs.H + u8iv; break; //ADC A, H
	case(0x8D): u8iv = flags.carry; SetFlags(Regs.A, Regs.L, u8iv, false, CARRYMODE::BOTH); Regs.A += Regs.L + u8iv; break; //ADC A, L
	case(0x8E): u8iv = flags.carry; u16iv = mmu->ReadByte(Regs.HL); SetFlags(Regs.A, u16iv, u8iv, false, CARRYMODE::BOTH); Regs.A += u16iv + u8iv; break; //ADC A, (HL)
	case(0x8F): u8iv = flags.carry; SetFlags(Regs.A, Regs.A, u8iv, false, CARRYMODE::BOTH); Regs.A += Regs.A + u8iv; break; //ADC A, A

	case(0x90): SetFlags(Regs.A, Regs.B, 0, true, CARRYMODE::BOTH); Regs.A -= Regs.B; break; //SUB A, B
	case(0x91): SetFlags(Regs.A, Regs.C, 0, true, CARRYMODE::BOTH); Regs.A -= Regs.C; break; //SUB A, C
	case(0x92): SetFlags(Regs.A, Regs.D, 0, true, CARRYMODE::BOTH); Regs.A -= Regs.D; break; //SUB A, D
	case(0x93): SetFlags(Regs.A, Regs.E, 0, true, CARRYMODE::BOTH); Regs.A -= Regs.E; break; //SUB A, E
	case(0x94): SetFlags(Regs.A, Regs.H, 0, true, CARRYMODE::BOTH); Regs.A -= Regs.H; break; //SUB A, H
	case(0x95): SetFlags(Regs.A, Regs.L, 0, true, CARRYMODE::BOTH); Regs.A -= Regs.L; break; //SUB A, L
	case(0x96): u8iv = mmu->ReadByte(Regs.HL); SetFlags(Regs.A, u8iv, 0, true, CARRYMODE::BOTH); Regs.A -= u8iv; break; //SUB A, (HL)
	case(0x97): SetFlags(Regs.A, Regs.A, 0, true, CARRYMODE::BOTH); Regs.A -= Regs.A; break; //SUB A, A

	case(0x98): u8iv = flags.carry; SetFlags(Regs.A, Regs.B, u8iv, true, CARRYMODE::BOTH); Regs.A -= Regs.B + u8iv; break; //SBC A, B
	case(0x99): u8iv = flags.carry; SetFlags(Regs.A, Regs.C, u8iv, true, CARRYMODE::BOTH); Regs.A -= Regs.C + u8iv; break; //SBC A, C
	case(0x9A): u8iv = flags.carry; SetFlags(Regs.A, Regs.D, u8iv, true, CARRYMODE::BOTH); Regs.A -= Regs.D + u8iv; break; //SBC A, D
	case(0x9B): u8iv = flags.carry; SetFlags(Regs.A, Regs.E, u8iv, true, CARRYMODE::BOTH); Regs.A -= Regs.E + u8iv; break; //SBC A, E
	case(0x9C): u8iv = flags.carry; SetFlags(Regs.A, Regs.H, u8iv, true, CARRYMODE::BOTH); Regs.A -= Regs.H + u8iv; break; //SBC A, H
	case(0x9D): u8iv = flags.carry; SetFlags(Regs.A, Regs.L, u8iv, true, CARRYMODE::BOTH); Regs.A -= Regs.L + u8iv; break; //SBC A, L
	case(0x9E): u8iv = flags.carry; u16iv = mmu->ReadByte(Regs.HL); SetFlags(Regs.A, u16iv, u8iv, true, CARRYMODE::BOTH); Regs.A -= u16iv + u8iv; break; //SBC A, (HL)
	case(0x9F): u8iv = flags.carry; SetFlags(Regs.A, Regs.A, u8iv, true, CARRYMODE::BOTH); Regs.A -= Regs.A + u8iv; break; //SBC A, A

	case(0xA0): Regs.A = Regs.A & Regs.B; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; // AND A, B
	case(0xA1): Regs.A = Regs.A & Regs.C; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; // AND A, C
	case(0xA2): Regs.A = Regs.A & Regs.D; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; // AND A, D
	case(0xA3): Regs.A = Regs.A & Regs.E; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; // AND A, E
	case(0xA4): Regs.A = Regs.A & Regs.H; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; // AND A, H
	case(0xA5): Regs.A = Regs.A & Regs.L; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; // AND A, L
	case(0xA6): u8iv = mmu->ReadByte(Regs.HL); Regs.A = Regs.A & u8iv; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; //AND A, (HL)
	case(0xA7): Regs.A = Regs.A & Regs.A; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; // AND A, A

	case(0xA8): Regs.A = Regs.A ^ Regs.B; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, B
	case(0xA9): Regs.A = Regs.A ^ Regs.C; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, C
	case(0xAA): Regs.A = Regs.A ^ Regs.D; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, D
	case(0xAB): Regs.A = Regs.A ^ Regs.E; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, E
	case(0xAC): Regs.A = Regs.A ^ Regs.H; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, H
	case(0xAD): Regs.A = Regs.A ^ Regs.L; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, L
	case(0xAE): Regs.A = Regs.A ^ mmu->ReadByte(Regs.HL); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, (HL)
	case(0xAF): Regs.A = Regs.A ^ Regs.A; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //XOR A, A

	case(0xB0): Regs.A |= Regs.B; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, B
	case(0xB1): Regs.A |= Regs.C; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, C
	case(0xB2): Regs.A |= Regs.D; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, D
	case(0xB3): Regs.A |= Regs.E; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, E
	case(0xB4): Regs.A |= Regs.H; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, H
	case(0xB5): Regs.A |= Regs.L; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, L
	case(0xB6): Regs.A |= mmu->ReadByte(Regs.HL); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, (HL)
	case(0xB7): Regs.A |= Regs.A; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, A
	
	case(0xB8): SetFlags(Regs.A, Regs.B, 0, true, CARRYMODE::BOTH); break; //CP A, B
	case(0xB9): SetFlags(Regs.A, Regs.C, 0, true, CARRYMODE::BOTH); break; //CP A, C
	case(0xBA): SetFlags(Regs.A, Regs.D, 0, true, CARRYMODE::BOTH); break; //CP A, D
	case(0xBB): SetFlags(Regs.A, Regs.E, 0, true, CARRYMODE::BOTH); break; //CP A, E
	case(0xBC): SetFlags(Regs.A, Regs.H, 0, true, CARRYMODE::BOTH); break; //CP A, H
	case(0xBD): SetFlags(Regs.A, Regs.L, 0, true, CARRYMODE::BOTH); break; //CP A, L
	case(0xBE): u8iv = mmu->ReadByte(Regs.HL); SetFlags(Regs.A, u8iv, 0, true, CARRYMODE::BOTH); break; //CP A, (HL)
	case(0xBF): SetFlags(Regs.A, Regs.A, 0, true, CARRYMODE::BOTH); break; //CP A, A
	
	case(0xC6): u8iv = mmu->ReadByte(PC++); SetFlags(Regs.A, u8iv, 0, false, CARRYMODE::BOTH); Regs.A += u8iv; break; // ADD A, u8
	case(0xCE): u8iv = mmu->ReadByte(PC++); u16iv = flags.carry; SetFlags(Regs.A, u8iv, flags.carry, false, CARRYMODE::BOTH); Regs.A += u8iv + (uint8_t)u16iv; break; // ADC A, u8
	case(0xD6): u8iv = mmu->ReadByte(PC++); SetFlags(Regs.A, u8iv, 0, true, CARRYMODE::BOTH); Regs.A -= u8iv; break; // SUB A, u8
	case(0xDE): u8iv = mmu->ReadByte(PC++); u16iv = flags.carry; SetFlags(Regs.A, u8iv, flags.carry, true, CARRYMODE::BOTH); Regs.A -= u8iv + (uint8_t)u16iv; break; // SBC A, u8

	case(0xE6): u8iv = mmu->ReadByte(PC); PC += 1; Regs.A = Regs.A & u8iv; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; //AND A, u8
	case(0xEE): Regs.A = Regs.A ^ mmu->ReadByte(PC++); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //XOR A, u8
	case(0xF6): Regs.A |= mmu->ReadByte(PC++); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // OR A, u8
	case(0xFE): u8iv = mmu->ReadByte(PC); PC += 1; SetFlags(Regs.A, u8iv, 0, true, CARRYMODE::BOTH); break; // CP A, u8

	//ALU 16-bit
	case(0x03): Regs.BC++; break; //INC BC
	case(0x13): Regs.DE++; break; //INC DE
	case(0x23): Regs.HL++; break; //INC HL
	case(0x33): SP++; break; //INC SP

	case(0x0B): Regs.BC--; break; //DEC BC
	case(0x1B): Regs.DE--; break; //DEC DE
	case(0x2B): Regs.HL--; break; //DEC HL
	case(0x3B): SP--; break; //DEC SP

	case(0x09): SetNeg(0); CalcCarry16(Regs.HL, Regs.BC, 0, false, CARRYMODE::BOTH); Regs.HL += Regs.BC; break; //ADD HL, BC
	case(0x19): SetNeg(0); CalcCarry16(Regs.HL, Regs.DE, 0, false, CARRYMODE::BOTH); Regs.HL += Regs.DE; break; //ADD HL, DE
	case(0x29): SetNeg(0); CalcCarry16(Regs.HL, Regs.HL, 0, false, CARRYMODE::BOTH); Regs.HL += Regs.HL; break; //ADD HL, HL
	case(0x39): SetNeg(0); CalcCarry16(Regs.HL, SP, 0, false, CARRYMODE::BOTH); Regs.HL += SP; break; //ADD HL, SP

	case(0xE8): i8iv = mmu->ReadByte(PC++); CalcCarry(SP, (uint16_t)i8iv, 0, false, CARRYMODE::BOTH); SetZero(0); SetNeg(0); SP += i8iv; break; // ADD SP, i8
	
	//Jumps
	case(0x18): i8iv = (mmu->ReadByte(PC++)); PC += i8iv; break; //JR i8
	case(0x20): i8iv = (mmu->ReadByte(PC++)); if (!flags.zero)  { CycleCounter += 4; PC += i8iv; } break; //JR NZ, i8
	case(0x28): i8iv = (mmu->ReadByte(PC++)); if (flags.zero)   { CycleCounter += 4; PC += i8iv; } break; //JR Z,  i8
	case(0x30): i8iv = (mmu->ReadByte(PC++)); if (!flags.carry) { CycleCounter += 4; PC += i8iv; } break; //JR NC, i8
	case(0x38): i8iv = (mmu->ReadByte(PC++)); if (flags.carry)  { CycleCounter += 4; PC += i8iv; } break; //JR C,  i8
	
	case(0xC0): if (!flags.zero) { CycleCounter += 12; PC = Pop(); } break; // RET NZ
	case(0xC2): u16iv = mmu->ReadWord(PC); PC += 2; if (!flags.zero) { CycleCounter += 4; PC = u16iv; } break; // JP NZ, u16
	case(0xC3): u16iv = mmu->ReadWord(PC); PC = u16iv; break; //JP u16
	case(0xC4): u16iv = mmu->ReadWord(PC); PC += 2; if (!flags.zero) { CycleCounter += 12; Push(PC); PC = u16iv; } break; //Call NZ, u16
	
	case(0xC8): if (flags.zero) { CycleCounter += 12; PC = Pop(); } break; // Ret Z
	case(0xC9): PC = Pop(); break; //Return
	case(0xCA): u16iv = mmu->ReadWord(PC); PC += 2; if (flags.zero) { CycleCounter += 4; PC = u16iv; } break; //JP Z, u16
	case(0xCC): u16iv = mmu->ReadWord(PC); PC += 2; if (flags.zero) { CycleCounter += 12; Push(PC); PC = u16iv; } break; // Call Z, u16
	case(0xCD): u16iv = mmu->ReadWord(PC); PC += 2; Push(PC); PC = u16iv; break; //Call
	
	case(0xD0): if (!flags.carry) { CycleCounter += 12; PC = Pop(); } break; // Ret NC
	case(0xD2): u16iv = mmu->ReadWord(PC); PC += 2; if (!flags.carry) { CycleCounter += 4; PC = u16iv; } break; // JP NC, u16
	case(0xD4): u16iv = mmu->ReadWord(PC); PC += 2; if (!flags.carry) { CycleCounter += 12; Push(PC); PC = u16iv; } break; //Call NC, u16
	case(0xD8): if (flags.carry) { CycleCounter += 12; PC = Pop(); } break; // Ret C
	case(0xD9): PC = Pop(); InterruptsEnabled = true; break; //RETI, Return and Enable Interrupts Immediately
	case(0xDA): u16iv = mmu->ReadWord(PC); PC += 2; if (flags.carry) { CycleCounter += 4; PC = u16iv; } break; //JP C, u16
	case(0xDC): u16iv = mmu->ReadWord(PC); PC += 2; if (flags.carry) { CycleCounter += 12; Push(PC); PC = u16iv; } break; // Call C, u16

	case(0xE9): PC = Regs.HL; break; //JP HL

	case(0xC7): Push(PC); PC = 0x00; break; // RST 0x00
	case(0xCF): Push(PC); PC = 0x08; break; // RST 0x08
	case(0xD7): Push(PC); PC = 0x10; break; // RST 0x10
	case(0xDF): Push(PC); PC = 0x18; break; // RST 0x18
	case(0xE7): Push(PC); PC = 0x20; break; // RST 0x20
	case(0xEF): Push(PC); PC = 0x28; break; // RST 0x28
	case(0xF7): Push(PC); PC = 0x30; break; // RST 0x30
	case(0xFF): Push(PC); PC = 0x38; break; // RST 0x38
	


	//The CB alternate-op table
	case(0xCB):
	{
		uint8_t subop = mmu->ReadByte(PC++);
		CycleCounter += CyclesPerOpCB[subop];
		switch (subop)
		{
		case(0x00): u8iv = Regs.B >> 7; Regs.B = Regs.B << 1; Regs.B |= u8iv; SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLC B
		case(0x01): u8iv = Regs.C >> 7; Regs.C = Regs.C << 1; Regs.C |= u8iv; SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLC C
		case(0x02): u8iv = Regs.D >> 7; Regs.D = Regs.D << 1; Regs.D |= u8iv; SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLC D
		case(0x03): u8iv = Regs.E >> 7; Regs.E = Regs.E << 1; Regs.E |= u8iv; SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLC E
		case(0x04): u8iv = Regs.H >> 7; Regs.H = Regs.H << 1; Regs.H |= u8iv; SetZero(Regs.H == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLC H
		case(0x05): u8iv = Regs.L >> 7; Regs.L = Regs.L << 1; Regs.L |= u8iv; SetZero(Regs.L == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLC L
		case(0x06): u8iv = mmu->ReadByte(Regs.HL); u16iv = u8iv >> 7; mmu->WriteByte(Regs.HL, (u8iv << 1) | u16iv); SetZero(mmu->ReadByte(Regs.HL) == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u16iv); break; // RLC (HL)
		case(0x07): u8iv = Regs.A >> 7; Regs.A = Regs.A << 1; Regs.A |= u8iv; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RLC A

		case(0x10): u8iv = Regs.B >> 7; Regs.B = Regs.B << 1; Regs.B |= (flags.carry ? 1 : 0); SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RL B
		case(0x11): u8iv = Regs.C >> 7; Regs.C = Regs.C << 1; Regs.C |= (flags.carry ? 1 : 0); SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RL C
		case(0x12): u8iv = Regs.D >> 7; Regs.D = Regs.D << 1; Regs.D |= (flags.carry ? 1 : 0); SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RL D
		case(0x13): u8iv = Regs.E >> 7; Regs.E = Regs.E << 1; Regs.E |= (flags.carry ? 1 : 0); SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RL E
		case(0x14): u8iv = Regs.H >> 7; Regs.H = Regs.H << 1; Regs.H |= (flags.carry ? 1 : 0); SetZero(Regs.H == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RL H
		case(0x15): u8iv = Regs.L >> 7; Regs.L = Regs.L << 1; Regs.L |= (flags.carry ? 1 : 0); SetZero(Regs.L == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RL L
		case(0x16): u8iv = mmu->ReadByte(Regs.HL); u16iv = u8iv >> 7; mmu->WriteByte(Regs.HL, (u8iv << 1) | (flags.carry ? 1 : 0)); SetZero(mmu->ReadByte(Regs.HL) == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u16iv); break; // RL (HL)
		case(0x17): u8iv = Regs.A >> 7; Regs.A = Regs.A << 1; Regs.A |= (flags.carry ? 1 : 0); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RL A

		case(0x08): u8iv = Regs.B & 1; Regs.B = Regs.B >> 1; Regs.B |= (u8iv << 7); SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRC B
		case(0x09): u8iv = Regs.C & 1; Regs.C = Regs.C >> 1; Regs.C |= (u8iv << 7); SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRC C
		case(0x0A): u8iv = Regs.D & 1; Regs.D = Regs.D >> 1; Regs.D |= (u8iv << 7); SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRC D
		case(0x0B): u8iv = Regs.E & 1; Regs.E = Regs.E >> 1; Regs.E |= (u8iv << 7); SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRC E
		case(0x0C): u8iv = Regs.H & 1; Regs.H = Regs.H >> 1; Regs.H |= (u8iv << 7); SetZero(Regs.H == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRC H
		case(0x0D): u8iv = Regs.L & 1; Regs.L = Regs.L >> 1; Regs.L |= (u8iv << 7); SetZero(Regs.L == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRC L
		case(0x0E): u8iv = mmu->ReadByte(Regs.HL); u16iv = u8iv & 1; mmu->WriteByte(Regs.HL, (u8iv >> 1) | (u16iv << 7)); SetZero(mmu->ReadByte(Regs.HL) == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u16iv); break; // RRC (HL)
		case(0x0F): u8iv = Regs.A & 1; Regs.A = Regs.A >> 1; Regs.A |= (u8iv << 7); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRC A

		case(0x18): u8iv = Regs.B & 1; Regs.B = Regs.B >> 1; Regs.B |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR B
		case(0x19): u8iv = Regs.C & 1; Regs.C = Regs.C >> 1; Regs.C |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR C
		case(0x1A): u8iv = Regs.D & 1; Regs.D = Regs.D >> 1; Regs.D |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR D
		case(0x1B): u8iv = Regs.E & 1; Regs.E = Regs.E >> 1; Regs.E |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR E
		case(0x1C): u8iv = Regs.H & 1; Regs.H = Regs.H >> 1; Regs.H |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.H == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR H
		case(0x1D): u8iv = Regs.L & 1; Regs.L = Regs.L >> 1; Regs.L |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.L == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR L
		case(0x1E): u8iv = mmu->ReadByte(Regs.HL); u16iv = u8iv & 1; mmu->WriteByte(Regs.HL, (u8iv >> 1) | ((flags.carry ? 1 : 0) << 7)); SetZero(mmu->ReadByte(Regs.HL) == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u16iv); break; // RR (HL)
		case(0x1F): u8iv = Regs.A & 1; Regs.A = Regs.A >> 1; Regs.A |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR A

		case(0x20): SetCarry(Regs.B >> 7); Regs.B <<= 1; SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); break; // SLA B
		case(0x21): SetCarry(Regs.C >> 7); Regs.C <<= 1; SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); break; // SLA C
		case(0x22): SetCarry(Regs.D >> 7); Regs.D <<= 1; SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); break; // SLA D
		case(0x23): SetCarry(Regs.E >> 7); Regs.E <<= 1; SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); break; // SLA E
		case(0x24): SetCarry(Regs.H >> 7); Regs.H <<= 1; SetZero(Regs.H == 0); SetNeg(0); SetHalfCarry(0); break; // SLA H
		case(0x25): SetCarry(Regs.L >> 7); Regs.L <<= 1; SetZero(Regs.L == 0); SetNeg(0); SetHalfCarry(0); break; // SLA L
		case(0x26): u8iv = mmu->ReadByte(Regs.HL); SetCarry(u8iv >> 7); mmu->WriteByte(Regs.HL, u8iv << 1); SetZero(mmu->ReadByte(Regs.HL) == 0); SetNeg(0); SetHalfCarry(0); break; // SLA (HL)
		case(0x27): SetCarry(Regs.A >> 7); Regs.A <<= 1; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); break; // SLA A

		case(0x28): u8iv = Regs.B >> 7; SetCarry(Regs.B & 1); Regs.B >>= 1; Regs.B |= u8iv << 7; SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); break; // SRA B
		case(0x29): u8iv = Regs.C >> 7; SetCarry(Regs.C & 1); Regs.C >>= 1; Regs.C |= u8iv << 7; SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); break; // SRA C
		case(0x2A): u8iv = Regs.D >> 7; SetCarry(Regs.D & 1); Regs.D >>= 1; Regs.D |= u8iv << 7; SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); break; // SRA D
		case(0x2B): u8iv = Regs.E >> 7; SetCarry(Regs.E & 1); Regs.E >>= 1; Regs.E |= u8iv << 7; SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); break; // SRA E
		case(0x2C): u8iv = Regs.H >> 7; SetCarry(Regs.H & 1); Regs.H >>= 1; Regs.H |= u8iv << 7; SetZero(Regs.H == 0); SetNeg(0); SetHalfCarry(0); break; // SRA H
		case(0x2D): u8iv = Regs.L >> 7; SetCarry(Regs.L & 1); Regs.L >>= 1; Regs.L |= u8iv << 7; SetZero(Regs.L == 0); SetNeg(0); SetHalfCarry(0); break; // SRA L
		case(0x2E): u8iv = mmu->ReadByte(Regs.HL); u16iv = u8iv >> 7; SetCarry(u8iv & 1); mmu->WriteByte(Regs.HL, (u8iv >> 1) | (u16iv << 7)); SetZero(mmu->ReadByte(Regs.HL) == 0); SetNeg(0); SetHalfCarry(0); break; // SRA (HL)
		case(0x2F): u8iv = Regs.A >> 7; SetCarry(Regs.A & 1); Regs.A >>= 1; Regs.A |= u8iv << 7; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); break; // SRA A

		case(0x30): u8iv = ((Regs.B >> 4) | (Regs.B << 4)); Regs.B = u8iv; SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //SWAP B
		case(0x31): u8iv = ((Regs.C >> 4) | (Regs.C << 4)); Regs.C = u8iv; SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //SWAP C
		case(0x32): u8iv = ((Regs.D >> 4) | (Regs.D << 4)); Regs.D = u8iv; SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //SWAP D
		case(0x33): u8iv = ((Regs.E >> 4) | (Regs.E << 4)); Regs.E = u8iv; SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //SWAP E
		case(0x34): u8iv = ((Regs.H >> 4) | (Regs.H << 4)); Regs.H = u8iv; SetZero(Regs.H == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //SWAP H
		case(0x35): u8iv = ((Regs.L >> 4) | (Regs.L << 4)); Regs.L = u8iv; SetZero(Regs.L == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //SWAP L
		case(0x36): u8iv = ((mmu->ReadByte(Regs.HL) >> 4) | (mmu->ReadByte(Regs.HL) << 4)); mmu->WriteByte(Regs.HL, u8iv); SetZero(mmu->ReadByte(Regs.HL) == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //SWAP (HL)
		case(0x37): u8iv = ((Regs.A >> 4) | (Regs.A << 4)); Regs.A = u8iv; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //SWAP A

		case(0x38): u8iv = Regs.B & 1; Regs.B = Regs.B >> 1; SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // SRL B
		case(0x39): u8iv = Regs.C & 1; Regs.C = Regs.C >> 1; SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // SRL C
		case(0x3A): u8iv = Regs.D & 1; Regs.D = Regs.D >> 1; SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // SRL D
		case(0x3B): u8iv = Regs.E & 1; Regs.E = Regs.E >> 1; SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // SRL E
		case(0x3C): u8iv = Regs.H & 1; Regs.H = Regs.H >> 1; SetZero(Regs.H == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // SRL H
		case(0x3D): u8iv = Regs.L & 1; Regs.L = Regs.L >> 1; SetZero(Regs.L == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // SRL L
		case(0x3E): u8iv = mmu->ReadByte(Regs.HL); u16iv = u8iv & 1; mmu->WriteByte(Regs.HL, u8iv >> 1); SetZero(mmu->ReadByte(Regs.HL) == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u16iv); break; // SRL (HL)
		case(0x3F): u8iv = Regs.A & 1; Regs.A = Regs.A >> 1; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // SRL A

		case(0x40): TestBit(Regs.B, 0); break; // BIT 0, B
		case(0x41): TestBit(Regs.C, 0); break; // BIT 0, C
		case(0x42): TestBit(Regs.D, 0); break; // BIT 0, D
		case(0x43): TestBit(Regs.E, 0); break; // BIT 0, E
		case(0x44): TestBit(Regs.H, 0); break; // BIT 0, H
		case(0x45): TestBit(Regs.L, 0); break; // BIT 0, L
		case(0x46): TestBit(mmu->ReadByte(Regs.HL), 0); break; // BIT 0, (HL)
		case(0x47): TestBit(Regs.A, 0); break; // BIT 0, A

		case(0x48): TestBit(Regs.B, 1); break; // BIT 1, B
		case(0x49): TestBit(Regs.C, 1); break; // BIT 1, C
		case(0x4A): TestBit(Regs.D, 1); break; // BIT 1, D
		case(0x4B): TestBit(Regs.E, 1); break; // BIT 1, E
		case(0x4C): TestBit(Regs.H, 1); break; // BIT 1, H
		case(0x4D): TestBit(Regs.L, 1); break; // BIT 1, L
		case(0x4E): TestBit(mmu->ReadByte(Regs.HL), 1); break; // BIT 1, (HL)
		case(0x4F): TestBit(Regs.A, 1); break; // BIT 1, A

		case(0x50): TestBit(Regs.B, 2); break; // BIT 2, B
		case(0x51): TestBit(Regs.C, 2); break; // BIT 2, C
		case(0x52): TestBit(Regs.D, 2); break; // BIT 2, D
		case(0x53): TestBit(Regs.E, 2); break; // BIT 2, E
		case(0x54): TestBit(Regs.H, 2); break; // BIT 2, H
		case(0x55): TestBit(Regs.L, 2); break; // BIT 2, L
		case(0x56): TestBit(mmu->ReadByte(Regs.HL), 2); break; // BIT 2, (HL)
		case(0x57): TestBit(Regs.A, 2); break; // BIT 2, A

		case(0x58): TestBit(Regs.B, 3); break; // BIT 3, B
		case(0x59): TestBit(Regs.C, 3); break; // BIT 3, C
		case(0x5A): TestBit(Regs.D, 3); break; // BIT 3, D
		case(0x5B): TestBit(Regs.E, 3); break; // BIT 3, E
		case(0x5C): TestBit(Regs.H, 3); break; // BIT 3, H
		case(0x5D): TestBit(Regs.L, 3); break; // BIT 3, L
		case(0x5E): TestBit(mmu->ReadByte(Regs.HL), 3); break; // BIT 3, (HL)
		case(0x5F): TestBit(Regs.A, 3); break; // BIT 3, A

		case(0x60): TestBit(Regs.B, 4); break; // BIT 4, B
		case(0x61): TestBit(Regs.C, 4); break; // BIT 4, C
		case(0x62): TestBit(Regs.D, 4); break; // BIT 4, D
		case(0x63): TestBit(Regs.E, 4); break; // BIT 4, E
		case(0x64): TestBit(Regs.H, 4); break; // BIT 4, H
		case(0x65): TestBit(Regs.L, 4); break; // BIT 4, L
		case(0x66): TestBit(mmu->ReadByte(Regs.HL), 4); break; // BIT 4, (HL)
		case(0x67): TestBit(Regs.A, 4); break; // BIT 4, A

		case(0x68): TestBit(Regs.B, 5); break; // BIT 5, B
		case(0x69): TestBit(Regs.C, 5); break; // BIT 5, C
		case(0x6A): TestBit(Regs.D, 5); break; // BIT 5, D
		case(0x6B): TestBit(Regs.E, 5); break; // BIT 5, E
		case(0x6C): TestBit(Regs.H, 5); break; // BIT 5, H
		case(0x6D): TestBit(Regs.L, 5); break; // BIT 5, L
		case(0x6E): TestBit(mmu->ReadByte(Regs.HL), 5); break; // BIT 5, (HL)
		case(0x6F): TestBit(Regs.A, 5); break; // BIT 5, A

		case(0x70): TestBit(Regs.B, 6); break; // BIT 6, B
		case(0x71): TestBit(Regs.C, 6); break; // BIT 6, C
		case(0x72): TestBit(Regs.D, 6); break; // BIT 6, D
		case(0x73): TestBit(Regs.E, 6); break; // BIT 6, E
		case(0x74): TestBit(Regs.H, 6); break; // BIT 6, H
		case(0x75): TestBit(Regs.L, 6); break; // BIT 6, L
		case(0x76): TestBit(mmu->ReadByte(Regs.HL), 6); break; // BIT 6, (HL)
		case(0x77): TestBit(Regs.A, 6); break; // BIT 6, A

		case(0x78): TestBit(Regs.B, 7); break; // BIT 7, B
		case(0x79): TestBit(Regs.C, 7); break; // BIT 7, C
		case(0x7A): TestBit(Regs.D, 7); break; // BIT 7, D
		case(0x7B): TestBit(Regs.E, 7); break; // BIT 7, E
		case(0x7C): TestBit(Regs.H, 7); break; // BIT 7, H
		case(0x7D): TestBit(Regs.L, 7); break; // BIT 7, L
		case(0x7E): TestBit(mmu->ReadByte(Regs.HL), 7); break; // BIT 7, (HL)
		case(0x7F): TestBit(Regs.A, 7); break; // BIT 7, A

		case(0x80): Regs.B = SetBit(Regs.B, 0, 0); break; // RES 0, B
		case(0x81): Regs.C = SetBit(Regs.C, 0, 0); break; // RES 0, C
		case(0x82): Regs.D = SetBit(Regs.D, 0, 0); break; // RES 0, D
		case(0x83): Regs.E = SetBit(Regs.E, 0, 0); break; // RES 0, E
		case(0x84): Regs.H = SetBit(Regs.H, 0, 0); break; // RES 0, H
		case(0x85): Regs.L = SetBit(Regs.L, 0, 0); break; // RES 0, L
		case(0x86): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 0, 0)); break; // RES 0, (HL)
		case(0x87): Regs.A = SetBit(Regs.A, 0, 0); break; // RES 0, A

		case(0x88): Regs.B = SetBit(Regs.B, 1, 0); break; // RES 1, B
		case(0x89): Regs.C = SetBit(Regs.C, 1, 0); break; // RES 1, C
		case(0x8A): Regs.D = SetBit(Regs.D, 1, 0); break; // RES 1, D
		case(0x8B): Regs.E = SetBit(Regs.E, 1, 0); break; // RES 1, E
		case(0x8C): Regs.H = SetBit(Regs.H, 1, 0); break; // RES 1, H
		case(0x8D): Regs.L = SetBit(Regs.L, 1, 0); break; // RES 1, L
		case(0x8E): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 1, 0)); break; // RES 1, (HL)
		case(0x8F): Regs.A = SetBit(Regs.A, 1, 0); break; // RES 1, A

		case(0x90): Regs.B = SetBit(Regs.B, 2, 0); break; // RES 2, B
		case(0x91): Regs.C = SetBit(Regs.C, 2, 0); break; // RES 2, C
		case(0x92): Regs.D = SetBit(Regs.D, 2, 0); break; // RES 2, D
		case(0x93): Regs.E = SetBit(Regs.E, 2, 0); break; // RES 2, E
		case(0x94): Regs.H = SetBit(Regs.H, 2, 0); break; // RES 2, H
		case(0x95): Regs.L = SetBit(Regs.L, 2, 0); break; // RES 2, L
		case(0x96): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 2, 0)); break; // RES 0, (HL)
		case(0x97): Regs.A = SetBit(Regs.A, 2, 0); break; // RES 2, A

		case(0x98): Regs.B = SetBit(Regs.B, 3, 0); break; // RES 3, B
		case(0x99): Regs.C = SetBit(Regs.C, 3, 0); break; // RES 3, C
		case(0x9A): Regs.D = SetBit(Regs.D, 3, 0); break; // RES 3, D
		case(0x9B): Regs.E = SetBit(Regs.E, 3, 0); break; // RES 3, E
		case(0x9C): Regs.H = SetBit(Regs.H, 3, 0); break; // RES 3, H
		case(0x9D): Regs.L = SetBit(Regs.L, 3, 0); break; // RES 3, L
		case(0x9E): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 3, 0)); break; // RES 3, (HL)
		case(0x9F): Regs.A = SetBit(Regs.A, 3, 0); break; // RES 3, A

		case(0xA0): Regs.B = SetBit(Regs.B, 4, 0); break; // RES 4, B
		case(0xA1): Regs.C = SetBit(Regs.C, 4, 0); break; // RES 4, C
		case(0xA2): Regs.D = SetBit(Regs.D, 4, 0); break; // RES 4, D
		case(0xA3): Regs.E = SetBit(Regs.E, 4, 0); break; // RES 4, E
		case(0xA4): Regs.H = SetBit(Regs.H, 4, 0); break; // RES 4, H
		case(0xA5): Regs.L = SetBit(Regs.L, 4, 0); break; // RES 4, L
		case(0xA6): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 4, 0)); break; // RES 4, (HL)
		case(0xA7): Regs.A = SetBit(Regs.A, 4, 0); break; // RES 4, A

		case(0xA8): Regs.B = SetBit(Regs.B, 5, 0); break; // RES 5, B
		case(0xA9): Regs.C = SetBit(Regs.C, 5, 0); break; // RES 5, C
		case(0xAA): Regs.D = SetBit(Regs.D, 5, 0); break; // RES 5, D
		case(0xAB): Regs.E = SetBit(Regs.E, 5, 0); break; // RES 5, E
		case(0xAC): Regs.H = SetBit(Regs.H, 5, 0); break; // RES 5, H
		case(0xAD): Regs.L = SetBit(Regs.L, 5, 0); break; // RES 5, L
		case(0xAE): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 5, 0)); break; // RES 5, (HL)
		case(0xAF): Regs.A = SetBit(Regs.A, 5, 0); break; // RES 5, A

		case(0xB0): Regs.B = SetBit(Regs.B, 6, 0); break; // RES 6, B
		case(0xB1): Regs.C = SetBit(Regs.C, 6, 0); break; // RES 6, C
		case(0xB2): Regs.D = SetBit(Regs.D, 6, 0); break; // RES 6, D
		case(0xB3): Regs.E = SetBit(Regs.E, 6, 0); break; // RES 6, E
		case(0xB4): Regs.H = SetBit(Regs.H, 6, 0); break; // RES 6, H
		case(0xB5): Regs.L = SetBit(Regs.L, 6, 0); break; // RES 6, L
		case(0xB6): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 6, 0)); break; // RES 6, (HL)
		case(0xB7): Regs.A = SetBit(Regs.A, 6, 0); break; // RES 6, A

		case(0xB8): Regs.B = SetBit(Regs.B, 7, 0); break; // RES 7, B
		case(0xB9): Regs.C = SetBit(Regs.C, 7, 0); break; // RES 7, C
		case(0xBA): Regs.D = SetBit(Regs.D, 7, 0); break; // RES 7, D
		case(0xBB): Regs.E = SetBit(Regs.E, 7, 0); break; // RES 7, E
		case(0xBC): Regs.H = SetBit(Regs.H, 7, 0); break; // RES 7, H
		case(0xBD): Regs.L = SetBit(Regs.L, 7, 0); break; // RES 7, L
		case(0xBE): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 7, 0)); break; // RES 7, (HL)
		case(0xBF): Regs.A = SetBit(Regs.A, 7, 0); break; // RES 7, A

		case(0xC0): Regs.B = SetBit(Regs.B, 0, 1); break; // SET 0, B
		case(0xC1): Regs.C = SetBit(Regs.C, 0, 1); break; // SET 0, C
		case(0xC2): Regs.D = SetBit(Regs.D, 0, 1); break; // SET 0, D
		case(0xC3): Regs.E = SetBit(Regs.E, 0, 1); break; // SET 0, E
		case(0xC4): Regs.H = SetBit(Regs.H, 0, 1); break; // SET 0, H
		case(0xC5): Regs.L = SetBit(Regs.L, 0, 1); break; // SET 0, L
		case(0xC6): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 0, 1)); break; // SET 0, (HL)
		case(0xC7): Regs.A = SetBit(Regs.A, 0, 1); break; // SET 0, A

		case(0xC8): Regs.B = SetBit(Regs.B, 1, 1); break; // SET 1, B
		case(0xC9): Regs.C = SetBit(Regs.C, 1, 1); break; // SET 1, C
		case(0xCA): Regs.D = SetBit(Regs.D, 1, 1); break; // SET 1, D
		case(0xCB): Regs.E = SetBit(Regs.E, 1, 1); break; // SET 1, E
		case(0xCC): Regs.H = SetBit(Regs.H, 1, 1); break; // SET 1, H
		case(0xCD): Regs.L = SetBit(Regs.L, 1, 1); break; // SET 1, L
		case(0xCE): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 1, 1)); break; // SET 1, (HL)
		case(0xCF): Regs.A = SetBit(Regs.A, 1, 1); break; // SET 1, A

		case(0xD0): Regs.B = SetBit(Regs.B, 2, 1); break; // SET 2, B
		case(0xD1): Regs.C = SetBit(Regs.C, 2, 1); break; // SET 2, C
		case(0xD2): Regs.D = SetBit(Regs.D, 2, 1); break; // SET 2, D
		case(0xD3): Regs.E = SetBit(Regs.E, 2, 1); break; // SET 2, E
		case(0xD4): Regs.H = SetBit(Regs.H, 2, 1); break; // SET 2, H
		case(0xD5): Regs.L = SetBit(Regs.L, 2, 1); break; // SET 2, L
		case(0xD6): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 2, 1)); break; // SET 0, (HL)
		case(0xD7): Regs.A = SetBit(Regs.A, 2, 1); break; // SET 2, A

		case(0xD8): Regs.B = SetBit(Regs.B, 3, 1); break; // SET 3, B
		case(0xD9): Regs.C = SetBit(Regs.C, 3, 1); break; // SET 3, C
		case(0xDA): Regs.D = SetBit(Regs.D, 3, 1); break; // SET 3, D
		case(0xDB): Regs.E = SetBit(Regs.E, 3, 1); break; // SET 3, E
		case(0xDC): Regs.H = SetBit(Regs.H, 3, 1); break; // SET 3, H
		case(0xDD): Regs.L = SetBit(Regs.L, 3, 1); break; // SET 3, L
		case(0xDE): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 3, 1)); break; // SET 3, (HL)
		case(0xDF): Regs.A = SetBit(Regs.A, 3, 1); break; // SET 3, A

		case(0xE0): Regs.B = SetBit(Regs.B, 4, 1); break; // SET 4, B
		case(0xE1): Regs.C = SetBit(Regs.C, 4, 1); break; // SET 4, C
		case(0xE2): Regs.D = SetBit(Regs.D, 4, 1); break; // SET 4, D
		case(0xE3): Regs.E = SetBit(Regs.E, 4, 1); break; // SET 4, E
		case(0xE4): Regs.H = SetBit(Regs.H, 4, 1); break; // SET 4, H
		case(0xE5): Regs.L = SetBit(Regs.L, 4, 1); break; // SET 4, L
		case(0xE6): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 4, 1)); break; // SET 4, (HL)
		case(0xE7): Regs.A = SetBit(Regs.A, 4, 1); break; // SET 4, A

		case(0xE8): Regs.B = SetBit(Regs.B, 5, 1); break; // SET 5, B
		case(0xE9): Regs.C = SetBit(Regs.C, 5, 1); break; // SET 5, C
		case(0xEA): Regs.D = SetBit(Regs.D, 5, 1); break; // SET 5, D
		case(0xEB): Regs.E = SetBit(Regs.E, 5, 1); break; // SET 5, E
		case(0xEC): Regs.H = SetBit(Regs.H, 5, 1); break; // SET 5, H
		case(0xED): Regs.L = SetBit(Regs.L, 5, 1); break; // SET 5, L
		case(0xEE): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 5, 1)); break; // SET 5, (HL)
		case(0xEF): Regs.A = SetBit(Regs.A, 5, 1); break; // SET 5, A

		case(0xF0): Regs.B = SetBit(Regs.B, 6, 1); break; // SET 6, B
		case(0xF1): Regs.C = SetBit(Regs.C, 6, 1); break; // SET 6, C
		case(0xF2): Regs.D = SetBit(Regs.D, 6, 1); break; // SET 6, D
		case(0xF3): Regs.E = SetBit(Regs.E, 6, 1); break; // SET 6, E
		case(0xF4): Regs.H = SetBit(Regs.H, 6, 1); break; // SET 6, H
		case(0xF5): Regs.L = SetBit(Regs.L, 6, 1); break; // SET 6, L
		case(0xF6): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 6, 1)); break; // SET 6, (HL)
		case(0xF7): Regs.A = SetBit(Regs.A, 6, 1); break; // SET 6, A

		case(0xF8): Regs.B = SetBit(Regs.B, 7, 1); break; // SET 7, B
		case(0xF9): Regs.C = SetBit(Regs.C, 7, 1); break; // SET 7, C
		case(0xFA): Regs.D = SetBit(Regs.D, 7, 1); break; // SET 7, D
		case(0xFB): Regs.E = SetBit(Regs.E, 7, 1); break; // SET 7, E
		case(0xFC): Regs.H = SetBit(Regs.H, 7, 1); break; // SET 7, H
		case(0xFD): Regs.L = SetBit(Regs.L, 7, 1); break; // SET 7, L
		case(0xFE): u8iv = mmu->ReadByte(Regs.HL); mmu->WriteByte(Regs.HL, SetBit(u8iv, 7, 1)); break; // SET 7, (HL)
		case(0xFF): Regs.A = SetBit(Regs.A, 7, 1); break; // SET 7, A

		//How?
		default:
			std::cout << "Undefined instruction: 0xCB 0x"
				<< std::hex << std::setfill('0') << std::uppercase << std::setw(2)
				<< (int)subop << std::endl;
			PrintCPUState();

			Stopped = true;
			break;
		}
		break;
	}

	
	//Uh-Oh
	default:
		std::cout << "Undefined instruction: 0x"
                  << std::hex << std::setfill('0') << std::uppercase << std::setw(2)
                  << (int)op << std::endl;
		PrintCPUState();

		Stopped = true;
		break;
	}

	return;
}

void Cpu::PrintCPUState()
{
	std::cout
	<< "A: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)Regs.A << ' '
	<< "F: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)Regs.F << ' '
	<< "B: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)Regs.B << ' '
	<< "C: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)Regs.C << ' '
	<< "D: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)Regs.D << ' '
	<< "E: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)Regs.E << ' '
	<< "H: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)Regs.H << ' '
	<< "L: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)Regs.L << ' '
	<< "SP: " << std::hex << std::setfill('0') << std::uppercase << std::setw(4) << (int)SP << ' '
	<< "PC: 00:" << std::hex << std::setfill('0') << std::uppercase << std::setw(4) << (int)PC << ' '
	<< "("
	<< std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(PC) << ' '
	<< std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(PC+1) << ' '
	<< std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(PC+2) << ' '
	<< std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(PC+3)
	<< ")"
	//<< " IME: " << (int)InterruptsEnabled
	//<< " IE: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(0xFFFF)
	//<< " IF: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(0xFF0F)
	//<< " DIV: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(0xFF04)
	//<< " TIMA: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(0xFF05)
	//<< " TMA: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(0xFF06)
	//<< " TAC: " << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (int)mmu->ReadByte(0xFF07)
	<< '\n';
}