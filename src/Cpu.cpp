#include "Cpu.h"
#include <cassert>
#include <iostream>
#include <iomanip>

Cpu::Cpu(Mmu* __mmu) : mmu(__mmu)
{
	if (!mmu->isBootRomEnabled()) //fake it til you make it
	{
		AF = 0x01B0; //0x11B0
		BC = 0x0013;
		DE = 0x00D8;
		HL = 0x014D;
		SP = 0xFFFE;
		PC = 0x0100;
	}

	//stub LY to 0x90 (line 144, begin VBlank)
	mmu->WriteByte(0xFF44, 0x90);

}

void Cpu::Tick()
{
	if (Stopped)
		return;

	if (Halted)
	{
		OpsCounter++;
		CycleCounter += CyclesPerOp[0x00];
		return;
	}

	uint8_t nextOp = mmu->ReadByte(PC);
	PC += 1;
	Execute(nextOp);

	//handle interrupts

	//PrintCPUState();
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

void Cpu::Execute(uint8_t op)
{
	OpsCounter++;
	CycleCounter += CyclesPerOp[op];

	//temporary immediate values
	uint8_t u8iv;
	int8_t i8iv;
	uint16_t u16iv;
	int16_t i16iv;

	switch (op)
	{
	//Misc
	case(0x00): break; //NOP
	case(0xF3): break; //DI TODO: actually disable interrupts

	//Rotate/Shift/Bitops
	case(0x0F): u8iv = Regs.A & 1; Regs.A = Regs.A >> 1; Regs.A |= ((u8iv ? 1 : 0) << 7);        SetZero(0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRCA
	case(0x1F): u8iv = Regs.A & 1; Regs.A = Regs.A >> 1; Regs.A |= ((flags.carry ? 1 : 0) << 7); SetZero(0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RRA
		
	//Load/Store/Move 8-bit
	case(0x02): mmu->WriteByte(BC, Regs.A); break; //LD (BC), A
	case(0x06): Regs.B = mmu->ReadByte(PC); PC += 1; break; //LD B, u8
	case(0x0A): Regs.A = mmu->ReadByte(BC); break; //LD A, (BC)
	case(0x0E): Regs.C = mmu->ReadByte(PC); PC += 1; break; //LD C, u8
	
	case(0x12): mmu->WriteByte(DE, Regs.A); break; //LD (DE), A
	case(0x16): Regs.D = mmu->ReadByte(PC); PC += 1; break; // LD D, u8
	case(0x1A): Regs.A = mmu->ReadByte(DE); break; //LD A, (DE)
	case(0x1E): Regs.E = mmu->ReadByte(PC); PC += 1; break; // LD E, u8
	
	case(0x22): mmu->WriteByte(HL++, Regs.A); break; // LD (HL+), A
	case(0x26): Regs.H = mmu->ReadByte(PC); PC += 1; break; //LD H, u8
	case(0x2A): Regs.A = mmu->ReadByte(HL++); break; //LD A, (HL+)
	case(0x2E): Regs.L = mmu->ReadByte(PC); PC += 1; break; //LD L, u8
	
	case(0x32): mmu->WriteByte(HL--, Regs.A); break; // LD (HL-), A
	case(0x36): mmu->WriteByte(HL, mmu->ReadByte(PC++)); break; //LD (HL), u8
	case(0x3A): Regs.A = mmu->ReadByte(HL--); break; //LD A, (HL-)
	case(0x3E): Regs.A = mmu->ReadByte(PC); PC += 1; break; //LD A, u8
	
	case(0x40): Regs.B = Regs.B; break; //LD B, B
	case(0x41): Regs.B = Regs.C; break; //LD B, C
	case(0x42): Regs.B = Regs.D; break; //LD B, D
	case(0x43): Regs.B = Regs.E; break; //LD B, E
	case(0x44): Regs.B = Regs.H; break; //LD B, H
	case(0x45): Regs.B = Regs.L; break; //LD B, L
	case(0x46): Regs.B = mmu->ReadByte(HL); break; // LD B, (HL)
	case(0x47): Regs.B = Regs.A; break; //LD B, A
	
	case(0x48): Regs.C = Regs.B; break; //LD C, B
	case(0x49): Regs.C = Regs.C; break; //LD C, C
	case(0x4A): Regs.C = Regs.D; break; //LD C, D
	case(0x4B): Regs.C = Regs.E; break; //LD C, E
	case(0x4C): Regs.C = Regs.H; break; //LD C, H
	case(0x4D): Regs.C = Regs.L; break; //LD C, L
	case(0x4E): Regs.C = mmu->ReadByte(HL); break; // LD C, (HL)
	case(0x4F): Regs.C = Regs.A; break; //LD C, A

	case(0x50): Regs.D = Regs.B; break; //LD D, B
	case(0x51): Regs.D = Regs.C; break; //LD D, C
	case(0x52): Regs.D = Regs.D; break; //LD D, D
	case(0x53): Regs.D = Regs.E; break; //LD D, E
	case(0x54): Regs.D = Regs.H; break; //LD D, H
	case(0x55): Regs.D = Regs.L; break; //LD D, L
	case(0x56): Regs.D = mmu->ReadByte(HL); break; // LD D, (HL)
	case(0x57): Regs.D = Regs.A; break; //LD D, A

	case(0x58): Regs.E = Regs.B; break; //LD E, B
	case(0x59): Regs.E = Regs.C; break; //LD E, C
	case(0x5A): Regs.E = Regs.D; break; //LD E, D
	case(0x5B): Regs.E = Regs.E; break; //LD E, E
	case(0x5C): Regs.E = Regs.H; break; //LD E, H
	case(0x5D): Regs.E = Regs.L; break; //LD E, L
	case(0x5E): Regs.E = mmu->ReadByte(HL); break; // LD E, (HL)
	case(0x5F): Regs.E = Regs.A; break; //LD E, A
	
	case(0x60): Regs.H = Regs.B; break; //LD H, B
	case(0x61): Regs.H = Regs.C; break; //LD H, C
	case(0x62): Regs.H = Regs.D; break; //LD H, D
	case(0x63): Regs.H = Regs.E; break; //LD H, E
	case(0x64): Regs.H = Regs.H; break; //LD H, H
	case(0x65): Regs.H = Regs.L; break; //LD H, L
	case(0x66): Regs.H = mmu->ReadByte(HL); break; // LD H, (HL)
	case(0x67): Regs.H = Regs.A; break; //LD D, A

	case(0x68): Regs.L = Regs.B; break; //LD L, B
	case(0x69): Regs.L = Regs.C; break; //LD L, C
	case(0x6A): Regs.L = Regs.D; break; //LD L, D
	case(0x6B): Regs.L = Regs.E; break; //LD L, E
	case(0x6C): Regs.L = Regs.H; break; //LD L, H
	case(0x6D): Regs.L = Regs.L; break; //LD L, L
	case(0x6E): Regs.L = mmu->ReadByte(HL); break; // LD L, (HL)
	case(0x6F): Regs.L = Regs.A; break; //LD L, A
	
	case(0x70): mmu->WriteByte(HL, Regs.B); break; // LD (HL), B
	case(0x71): mmu->WriteByte(HL, Regs.C); break; // LD (HL), C
	case(0x72): mmu->WriteByte(HL, Regs.D); break; // LD (HL), D
	case(0x73): mmu->WriteByte(HL, Regs.E); break; // LD (HL), E
	case(0x74): mmu->WriteByte(HL, Regs.H); break; // LD (HL), H
	case(0x75): mmu->WriteByte(HL, Regs.L); break; // LD (HL), L
	//   0x76   HALT
	case(0x77): mmu->WriteByte(HL, Regs.A); break; // LD (HL), A
	
	case(0x78): Regs.A = Regs.B; break; //LD A, B
	case(0x79): Regs.A = Regs.C; break; //LD A, C
	case(0x7A): Regs.A = Regs.D; break; //LD A, D
	case(0x7B): Regs.A = Regs.E; break; //LD A, E
	case(0x7C): Regs.A = Regs.H; break; //LD A, H
	case(0x7D): Regs.A = Regs.L; break; //LD A, L
	case(0x7E): Regs.A = mmu->ReadByte(HL); break; // LD A, (HL)
	case(0x7F): Regs.A = Regs.A; break; //LD A, A
	
	case(0xE0): mmu->WriteByte(0xFF00 + mmu->ReadByte(PC++), Regs.A); break; //LD (FF00+u8), A
	case(0xE2): mmu->WriteByte(0xFF00 + Regs.C, Regs.A); break; //LD (FF00+C), A
	case(0xEA): mmu->WriteByte(mmu->ReadWord(PC), Regs.A); PC += 2; break; //LD (u16), A
	
	case(0xF0): u8iv = mmu->ReadByte(PC); PC += 1; Regs.A = mmu->ReadByte(0xFF00 + u8iv); break; //LD A, (FF00+u8)
	case(0xF2): Regs.A = mmu->ReadByte(0xFF00 + Regs.C); break; //LD A, (FF00+C)
	case(0xFA): u16iv = mmu->ReadWord(PC); PC += 2; Regs.A = mmu->ReadByte(u16iv); break; // LD A, (u16)

	//Load/Store/Move 16-bit
	case(0x01): BC = mmu->ReadWord(PC); PC += 2; break; //LD BC, u16
	case(0x11): DE = mmu->ReadWord(PC); PC += 2; break; //LD DE, u16
	case(0x21): HL = mmu->ReadWord(PC); PC += 2; break; //LD HL, u16
	case(0x31): SP = mmu->ReadWord(PC); PC += 2; break; //LD SP, u16
	case(0xC1): BC = Pop(); break; //Pop  BC
	case(0xC5): Push(BC); break;   //Push BC	
	case(0xD1): DE = Pop(); break; //Pop  DE
	case(0xD5): Push(DE); break;   //Push DE
	case(0xE1): HL = Pop(); break; //Pop  HL
	case(0xE5): Push(HL); break;   //Push HL
	case(0xF1): AF = Pop(); break; //Pop  AF
	case(0xF5): Push(AF); break;   //Push AF

	case(0x08): mmu->WriteWord(mmu->ReadWord(PC), SP); PC += 2; break; //LD (u16), SP
	case(0xF9): SP = HL; break; //LD SP, HL

	//ALU 8-bit

	case(0x05): SetFlags(Regs.B, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.B--; break; //DEC B
	case(0x0D): SetFlags(Regs.C, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.C--; break; //DEC C
	case(0x14): SetFlags(Regs.D, 1, 0, false, CARRYMODE::HALFCARRY); Regs.D++; break; //INC D
	case(0x1C): SetFlags(Regs.E, 1, 0, false, CARRYMODE::HALFCARRY); Regs.E++; break; //INC E
	case(0x1D): SetFlags(Regs.E, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.E--; break; //DEC E
	case(0x24): SetFlags(Regs.H, 1, 0, false, CARRYMODE::HALFCARRY); Regs.H++; break; //INC H
	case(0x25): SetFlags(Regs.H, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.H--; break; //DEC H
	case(0x2C): SetFlags(Regs.L, 1, 0, false, CARRYMODE::HALFCARRY); Regs.L++; break; //INC L
	case(0x2D): SetFlags(Regs.L, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.L--; break; //DEC L
	case(0x35): SetFlags(mmu->ReadByte(HL), 1, 0, true, CARRYMODE::HALFCARRY);  mmu->WriteByte(HL, mmu->ReadByte(HL)-1); break; //DEC (HL)
	case(0x3C): SetFlags(Regs.A, 1, 0, false, CARRYMODE::HALFCARRY); Regs.A++; break; //INC A
	case(0x3D): SetFlags(Regs.A, 1, 0, true,  CARRYMODE::HALFCARRY); Regs.A--; break; //DEC A

	case(0xC6): u8iv = mmu->ReadByte(PC++); SetFlags(Regs.A, u8iv, 0, false, CARRYMODE::BOTH); Regs.A += u8iv; break; // ADD A, u8
	case(0xCE): u8iv = mmu->ReadByte(PC++); u16iv = flags.carry; SetFlags(Regs.A, u8iv, flags.carry, false, CARRYMODE::BOTH); Regs.A += u8iv + (uint8_t)u16iv; break; // ADC A, u8

	case(0xD6): u8iv = mmu->ReadByte(PC++); SetFlags(Regs.A, u8iv, 0, true, CARRYMODE::BOTH); Regs.A -= u8iv; break; // SUB A, u8

	case(0xB0): Regs.A |= Regs.B; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, B
	case(0xB1): Regs.A |= Regs.C; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, C
	case(0xB6): Regs.A |= mmu->ReadByte(HL); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, (HL)
	case(0xB7): Regs.A |= Regs.A; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //OR A, A

	case(0xA9): Regs.A = Regs.A ^ Regs.C; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, C
	case(0xAD): Regs.A = Regs.A ^ Regs.L; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, L
	case(0xAE): Regs.A = Regs.A ^ mmu->ReadByte(HL); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; // XOR A, (HL)
	case(0xAF): Regs.A = Regs.A ^ Regs.A; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //XOR A, A
	case(0xEE): Regs.A = Regs.A ^ mmu->ReadByte(PC++); SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(0); SetCarry(0); break; //XOR A, u8

	case(0xE6): u8iv = mmu->ReadByte(PC); PC += 1; Regs.A = Regs.A & u8iv; SetZero(Regs.A == 0); SetNeg(0); SetHalfCarry(1); SetCarry(0); break; //AND A, u8

	case(0xFE): u8iv = mmu->ReadByte(PC); PC += 1; SetFlags(Regs.A, u8iv, 0, true, CARRYMODE::BOTH); break; // CP A, u8

	//ALU 16-bit
	case(0x03): BC++; break; //INC BC
	case(0x13): DE++; break; //INC DE
	case(0x23): HL++; break; //INC HL

	case(0x29): SetNeg(0); CalcCarry16(HL, HL, 0, false, CARRYMODE::BOTH); HL += HL; break; //ADD HL, HL
	
	//Jumps
	case(0x18): i8iv = (mmu->ReadByte(PC++)); PC += i8iv; break; //JR i8
	case(0x20): i8iv = (mmu->ReadByte(PC++)); if (!flags.zero)  { CycleCounter += 4; PC += i8iv; } break; //JR NZ, i8
	case(0x28): i8iv = (mmu->ReadByte(PC++)); if (flags.zero)   { CycleCounter += 4; PC += i8iv; } break; //JR Z,  i8
	case(0x30): i8iv = (mmu->ReadByte(PC++)); if (!flags.carry) { CycleCounter += 4; PC += i8iv; } break; //JR NC, i8
	case(0x38): i8iv = (mmu->ReadByte(PC++)); if (flags.carry)  { CycleCounter += 4; PC += i8iv; } break; //JR C,  i8
	case(0xC3): PC = mmu->ReadWord(PC); break; //JP u16
	case(0xC4): u16iv = mmu->ReadWord(PC); PC += 2; if (!flags.zero) { CycleCounter += 12; Push(PC); PC = u16iv; } break; //Call NZ, u16
	case(0xC8): if (flags.zero) { CycleCounter += 12; PC = Pop(); } break; // Ret Z
	case(0xC9): PC = Pop(); break; //Return
	case(0xCD): u16iv = mmu->ReadWord(PC); PC += 2; Push(PC); PC = u16iv; break; //Call
	case(0xD0): if (!flags.carry) { CycleCounter += 12; PC = Pop(); } break; // Ret NC
	case(0xD8): if (flags.carry) { CycleCounter += 12; PC = Pop(); } break; // Ret C
	case(0xE9): PC = HL; break; //JP HL

	//The CB alternate-op table
	case(0xCB):
	{
		uint8_t subop = mmu->ReadByte(PC++);
		CycleCounter += CyclesPerOpCB[subop];
		switch (subop)
		{
		case(0x19): u8iv = Regs.C & 1; Regs.C = Regs.C >> 1; Regs.C |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.C == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR C
		case(0x1A): u8iv = Regs.D & 1; Regs.D = Regs.D >> 1; Regs.D |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.D == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR D
		case(0x1B): u8iv = Regs.E & 1; Regs.E = Regs.E >> 1; Regs.E |= ((flags.carry ? 1 : 0) << 7); SetZero(Regs.E == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // RR E
		case(0x38): u8iv = Regs.B & 1; Regs.B = Regs.B >> 1; SetZero(Regs.B == 0); SetNeg(0); SetHalfCarry(0); SetCarry(u8iv); break; // SRL B
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
	<< ")" << std::endl;
}
