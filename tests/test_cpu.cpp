#include <gtest/gtest.h>
#include <Nes.h>
#include "NesBus.h"
#include "NesCpu.h"
#include "NesCartridge.h"
#include "Mapper.h"
#include "NROM.h"
#include "SharedContext.h"

namespace BlueNESTest
{
    class MyEnv : public ::testing::Test {
    public:
        void SetUp() override {
            nes = new Nes(ctx);
            cart = nes->cart_;
            cart->mapper = new NROM(cart);
            bus = nes->bus_;
            cart->mapper->register_memory(*bus);
            cart->mapper->m_prgRamData.resize(0x2000);
            cpu = nes->cpu_;
            cpu->init_cpu();
            uint8_t rom[0x8000];
            cart->mapper->SetPRGRom(rom, sizeof(rom));
            uint8_t chr[0x2000];
            cart->mapper->SetCHRRom(chr, sizeof(chr));
            cart->mapper->RecomputeMappings();
            cpu->PowerCycle();
            cpu->SetPC(0x8000);
        }
        void TearDown() override {
        }
        void RunInst() {
            bool first = true;
            while (first || !cpu->inst_complete) {
                first = false;
                cpu->cpu_tick();
            }
        }
        SharedContext ctx;
        Nes* nes;
        NesBus* bus;
        NesCpu* cpu;
        NesCartridge* cart;
    };

    TEST(SampleTest, PlaceholderTest)
    {
        EXPECT_EQ(1, 1);
    }

    TEST_F(MyEnv, TestHardwareNMIImmediate)
	{
		uint8_t rom[0x8000];
		rom[0] = ADC_IMMEDIATE;
		rom[1] = 0x20;
		rom[0xFFFA - 0x8000] = 0x00;
		rom[0xFFFB - 0x8000] = 0x90;
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetNMIImmediate();
		uint8_t p = cpu->GetStatus();
		RunInst();
		// Now ensure NMI has triggered and PC is at NMI vector
		EXPECT_EQ((uint16_t)0x9000, cpu->GetPC());
		// The return address (0x8000) should be on the stack
		// It is 8000 because ADC_IMMEDIATE was interrupted.
		uint8_t new_p = bus->read(0x0100 + cpu->GetSP() + 1);
		uint8_t lo = bus->read(0x0100 + cpu->GetSP() + 2);
		uint8_t hi = bus->read(0x0100 + cpu->GetSP() + 3);
		EXPECT_EQ((uint8_t)((p & 0xEF) | 0x20), new_p);
		EXPECT_EQ(0x8000, (hi << 8) | lo);
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		// Just the NMI was run = 7 cycles
		EXPECT_EQ(cpu->GetCycleCount(), 7);
	}

	TEST_F(MyEnv, TestHardwareNMIPriorityOverIRQ)
	{
		uint8_t rom[0x8000];
		rom[0] = ADC_IMMEDIATE;
		rom[1] = 0x20;
		rom[0xFFFA - 0x8000] = 0x00; // NMI vector
		rom[0xFFFB - 0x8000] = 0x90;
		rom[0xFFFE - 0x8000] = 0x00; // IRQ vector
		rom[0xFFFF - 0x8000] = 0x91;
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetNMIImmediate();
		cpu->SetIRQImmediate(); // IRQ should be ignored because NMI has higher priority
		uint8_t p = cpu->GetStatus();
		RunInst();
		// Now ensure NMI has triggered and PC is at NMI vector
		EXPECT_EQ((uint16_t)0x9000, cpu->GetPC());
		// The return address (0x8000) should be on the stack
		// It is 8000 because ADC_IMMEDIATE was interrupted.
		uint8_t new_p = bus->read(0x0100 + cpu->GetSP() + 1);
		uint8_t lo = bus->read(0x0100 + cpu->GetSP() + 2);
		uint8_t hi = bus->read(0x0100 + cpu->GetSP() + 3);
		EXPECT_EQ((uint8_t)((p & 0xEF) | 0x20), new_p);
		EXPECT_EQ(0x8000, (hi << 8) | lo);
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		// Just the NMI was run = 7 cycles
		EXPECT_EQ(cpu->GetCycleCount(), 7);
	}

	TEST_F(MyEnv, TestHardwareIRQInsideNMIBlocked)
	{
		uint8_t rom[0x8000];
		rom[0x1000] = NOP_IMPLIED;
		rom[0x1001] = NOP_IMPLIED;
		//rom[0x9000 - 0x8000] = NOP_IMPLIED; // IRQ handler does nothing
		rom[0xFFFA - 0x8000] = 0x00; // NMI vector
		rom[0xFFFB - 0x8000] = 0x90;
		rom[0x9100 - 0x8000] = NOP_IMPLIED; // NMI handler does nothing
		rom[0xFFFE - 0x8000] = 0x00; // IRQ vector
		rom[0xFFFF - 0x8000] = 0x91;
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetNMIImmediate();
		RunInst();
		// Ensure we are inside NMI with interrupts disabled
		EXPECT_EQ((uint16_t)0x9000, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		// Attempt to trigger IRQ inside NMI with interrupts disabled
		cpu->setIRQ(true);
		// Run two instructions because of the delayed IRQ effect
		RunInst();
		RunInst();
		// Instead of jumping to the IRQ vector, we should have just run the NOP's at 0x9000 and 0x9001.
		EXPECT_EQ((uint16_t)0x9002, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		uint8_t p = cpu->GetStatus();
		// Now ensure NMI has triggered and PC is at NMI vector
			
		// The return address (0x8000) should be on the stack
		// It is 8000 because ADC_IMMEDIATE was interrupted.
		uint8_t new_p = bus->read(0x0100 + cpu->GetSP() + 1);
		uint8_t lo = bus->read(0x0100 + cpu->GetSP() + 2);
		uint8_t hi = bus->read(0x0100 + cpu->GetSP() + 3);
		EXPECT_EQ((uint8_t)((p & 0xEF) | 0x20), new_p);
		EXPECT_EQ(0x8000, (hi << 8) | lo);
		
		// NMI + NOP + NOP = 7 + 2 + 2 = 11 cycles
		EXPECT_EQ(cpu->GetCycleCount(), 11);
	}

	TEST_F(MyEnv, TestBRKInsideNMINotBlocked)
	{
		uint8_t rom[0x8000];
		rom[0x1000] = BRK_IMPLIED;
		//rom[0x9000 - 0x8000] = NOP_IMPLIED; // IRQ handler does nothing
		rom[0xFFFA - 0x8000] = 0x00; // NMI vector
		rom[0xFFFB - 0x8000] = 0x90;
		rom[0x9100 - 0x8000] = NOP_IMPLIED; // NMI handler does nothing
		rom[0xFFFE - 0x8000] = 0x00; // IRQ vector
		rom[0xFFFF - 0x8000] = 0x91;
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetNMIImmediate();
		RunInst();
		// Ensure we are inside NMI with interrupts disabled
		EXPECT_EQ((uint16_t)0x9000, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		// Run the BRK inside NMI with interrupts disabled
		RunInst();
		// We are now at the IRQ vector since BRK always triggers IRQ/BRK vector
		EXPECT_EQ((uint16_t)0x9100, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		uint8_t p = cpu->GetStatus();
		// Now ensure NMI has triggered and PC is at NMI vector

		// The return address (0x8000) should be on the stack
		// It is 8000 because ADC_IMMEDIATE was interrupted.
		uint8_t new_p = bus->read(0x0100 + cpu->GetSP() + 1);
		uint8_t lo = bus->read(0x0100 + cpu->GetSP() + 2);
		uint8_t hi = bus->read(0x0100 + cpu->GetSP() + 3);
		EXPECT_EQ((uint8_t)((p & 0xEF) | 0x20 | FLAG_BREAK), new_p);
		// The return address should be 0x9002 since BRK was executed at 0x9000
		EXPECT_EQ(0x9002, (hi << 8) | lo);

		// NMI + BRK = 7 + 7 = 14 cycles
		EXPECT_EQ(cpu->GetCycleCount(), 14);
	}

	// "it's really the status of the interrupt lines at the end of the second-to-last cycle that matters."
	// To test this, we first set the NMI line low.
	// Then we run an instruction that takes 2 cycles(ADC IMM), but it could be any instruction.
	// During that execution, the CPU will have had enough time to sample the NMI line.
	// We run another instruction (another ADC IMM) which should be interrupted by NMI.
	// It does not matter what the second instruction is, as long as it takes at least one cycle.
	TEST_F(MyEnv, TestHardwareNMI)
	{
		uint8_t rom[0x8000];
		rom[0] = ADC_IMMEDIATE;
		rom[1] = 0x20;
		rom[2] = ADC_IMMEDIATE;
		rom[3] = 0x30;
		rom[0xFFFA - 0x8000] = 0x00;
		rom[0xFFFB - 0x8000] = 0x90;
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->setNMI(true);
		uint8_t p = cpu->GetStatus();
		RunInst();
		// Ensure NMI has NOT triggered. The PC should be at 0x8002 after first ADC.
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		RunInst();
		EXPECT_EQ((uint16_t)0x9000, cpu->GetPC());
		// The return address (0x8002) should be on the stack
		uint8_t new_p = bus->read(0x0100 + cpu->GetSP() + 1);
		EXPECT_EQ((uint8_t)((p & 0xEF) | 0x20), new_p);
		uint8_t lo = bus->read(0x0100 + cpu->GetSP() + 2);
		uint8_t hi = bus->read(0x0100 + cpu->GetSP() + 3);
		EXPECT_EQ(0x8002, (hi << 8) | lo);
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		// First ADC IMM = 2, NMI = 7
		EXPECT_EQ(cpu->GetCycleCount(), 9);
	}

	TEST_F(MyEnv, TestHardwareIRQImmediate)
	{
		uint8_t rom[0x8000];
		rom[0] = ADC_IMMEDIATE;
		rom[1] = 0x20;
		rom[0xFFFE - 0x8000] = 0x00;
		rom[0xFFFF - 0x8000] = 0x90;
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetIRQImmediate();
		cpu->ClearFlag(FLAG_INTERRUPT); // Clear interrupt flag to allow BRK to proceed
		uint8_t p = cpu->GetStatus();
		RunInst();
		// Now ensure BRK has triggered and PC is at IRQ vector
		EXPECT_EQ((uint16_t)0x9000, cpu->GetPC());
		// The return address (0x8000) should be on the stack
		// It is 8000 because ADC_IMMEDIATE was interrupted.
		uint8_t new_p = bus->read(0x0100 + cpu->GetSP() + 1);
		uint8_t lo = bus->read(0x0100 + cpu->GetSP() + 2);
		uint8_t hi = bus->read(0x0100 + cpu->GetSP() + 3);
		EXPECT_EQ((uint8_t)((p & 0xEF) | 0x20), new_p);
		EXPECT_EQ(0x8000, (hi << 8) | lo);
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		// Just the IRQ was run = 7 cycles
		EXPECT_EQ(cpu->GetCycleCount(), 7);
	}
		
	TEST_F(MyEnv, TestHardwareIRQ)
	{
		uint8_t rom[0x8000];
		rom[0] = ADC_IMMEDIATE;
		rom[1] = 0x20;
		rom[2] = ADC_IMMEDIATE;
		rom[3] = 0x30;
		rom[0xFFFE - 0x8000] = 0x00;
		rom[0xFFFF - 0x8000] = 0x90;
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->setIRQ(true);
		cpu->ClearFlag(FLAG_INTERRUPT); // Clear interrupt flag to allow BRK to proceed
		uint8_t p = cpu->GetStatus();
		RunInst();
		// Ensure BRK has NOT triggered. The PC should be at 0x8002 after first ADC.
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		RunInst();
		// Now ensure BRK has triggered and PC is at IRQ vector
		EXPECT_EQ((uint16_t)0x9000, cpu->GetPC());
		// The return address (0x8002) should be on the stack
		uint8_t new_p = bus->read(0x0100 + cpu->GetSP() + 1);
		uint8_t lo = bus->read(0x0100 + cpu->GetSP() + 2);
		uint8_t hi = bus->read(0x0100 + cpu->GetSP() + 3);
		EXPECT_EQ((uint8_t)((p & 0xEF) | 0x20), new_p);
		EXPECT_EQ(0x8002, (hi << 8) | lo);
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		// First ADC IMM = 2, BRK = 7
		EXPECT_EQ(cpu->GetCycleCount(), 9);
	}

    TEST_F(MyEnv, TestADCImmediate1)
    {
        uint8_t rom[] = { ADC_IMMEDIATE, 0x20 };
        cart->mapper->SetPRGRom(rom, sizeof(rom));

        RunInst();
		EXPECT_EQ((uint8_t)0x20, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
    }

	TEST_F(MyEnv, TestADCImmediateWithCarry)
	{
		uint8_t rom[] = { ADC_IMMEDIATE, 0x20 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0x11);
		cpu->SetFlag(FLAG_CARRY);
		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x32, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestADCImmediateWithOverflow)
	{
		// $70 + $20 will result in signed overflow
		cpu->SetA(0x70);
		uint8_t rom[] = { ADC_IMMEDIATE, 0x20 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		RunInst();
		EXPECT_EQ((uint8_t)0x90, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestADCZeroPage)
	{
		// Add what is at zero page 0x15 to A.
		uint8_t rom[] = { ADC_ZEROPAGE, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x69);
		cpu->SetA(0x18);
		RunInst();
		EXPECT_EQ((uint8_t)0x81, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestADCZeroPage_X)
	{
		// Add what is at zero page 0x15 to A.
		uint8_t rom[] = { ADC_ZEROPAGE_X, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0016, 0x69);
		cpu->SetA(0x18);
		cpu->SetX(0x1);
		RunInst();
		EXPECT_EQ((uint8_t)0x81, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestADCAbsolute)
	{
		// I think the 6502 stores in little endian, need to double check
		uint8_t rom[] = { ADC_ABSOLUTE, 0x23, 0x3 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x323, 0x40);
		cpu->SetA(0x20);
		RunInst();
		EXPECT_EQ((uint8_t)0x60, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestADCNonNegative)
	{
		// I think the 6502 stores in little endian, need to double check
		uint8_t rom[] = { ADC_ABSOLUTE, 0x23, 0x3 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x323, 0x40);
		cpu->SetA(0x20);
		cpu->SetFlag(FLAG_ZERO);
		RunInst();
		EXPECT_EQ((uint8_t)0x60, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}
	TEST_F(MyEnv, TestADCImmediateNegativeResult)
	{
		uint8_t rom[] = { ADC_IMMEDIATE, 0x90 }; // 144 decimal
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0x50); // 80 decimal
		RunInst();
		// 80 + 144 = 224 which is negative in signed 8-bit
		EXPECT_EQ((uint8_t)0xE0, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestANDImmediate)
	{
		uint8_t rom[] = { AND_IMMEDIATE, 0x7 }; // 0111
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0xE); // 1110
		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x6, cpu->GetA()); // 0110
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestANDZeroPage)
	{
		bus->write(0x0035, 0x07); // 0111
		uint8_t rom[] = { AND_ZEROPAGE, 0x35 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0xE); // 1110

		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x6, cpu->GetA()); // 0110
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestANDZeroPageX)
	{
		bus->write(0x0035, 0x07); // 0111
		uint8_t rom[] = { AND_ZEROPAGE_X, 0x34 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetX(0x1);
		cpu->SetA(0xE); // 1110

		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x6, cpu->GetA()); // 0110
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestANDAbsolute)
	{
		bus->write(0x0235, 0x07); // 0111
		uint8_t rom[] = { AND_ABSOLUTE, 0x35, 0x02 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0xE); // 1110

		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x6, cpu->GetA()); // 0110
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestANDAbsoluteX)
	{
		bus->write(0x0235, 0x07); // 0111
		uint8_t rom[] = { AND_ABSOLUTE_X, 0x33, 0x02 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0xE); // 1110
		cpu->SetX(0x2);

		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x6, cpu->GetA()); // 0110
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestANDAbsoluteY)
	{
		bus->write(0x0235, 0x07); // 0111
		uint8_t rom[] = { AND_ABSOLUTE_Y, 0x33, 0x02 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0xE); // 1110
		cpu->SetY(0x2);

		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x6, cpu->GetA()); // 0110
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestANDIndexedIndirect)
	{
		bus->write(0x0035, 0x35);
		bus->write(0x0036, 0x02); // Pointer to 0x0235
		bus->write(0x0235, 0x07); // 0111
		uint8_t rom[] = { AND_INDEXEDINDIRECT, 0x33 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0xE); // 1110
		cpu->SetX(0x2);

		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x6, cpu->GetA()); // 0110
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestANDIndirectIndexed)
	{
		bus->write(0x0035, 0x35);
		bus->write(0x0036, 0x02); // Pointer to 0x0235
		bus->write(0x0237, 0x07); // 0111
		uint8_t rom[] = { AND_INDIRECTINDEXED, 0x35 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0xE); // 1110
		cpu->SetY(0x2);
		RunInst();
		// 0x11 + 0x20 + 1 (Carry) = 0x32
		EXPECT_EQ((uint8_t)0x6, cpu->GetA()); // 0110
		EXPECT_EQ(cpu->GetCycleCount(), 5);
	}

	TEST_F(MyEnv, TestASLAccumulator)
	{
		uint8_t rom[] = { ASL_ACCUMULATOR };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0x45); // 0100 0101
		RunInst();
		// Shift left: 1000 1010
		EXPECT_EQ((uint8_t)0x8A, cpu->GetA());
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestASLZeroPage)
	{
		uint8_t rom[] = { ASL_ZEROPAGE, 0x10 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0010, 0x45); // 0100 0101
		RunInst();
		// Shift left: 1000 1010
		EXPECT_EQ((uint8_t)0x8A, bus->read(0x0010));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 5);
	}

	TEST_F(MyEnv, TestASLZeroPageX)
	{
		uint8_t rom[] = { ASL_ZEROPAGE_X, 0x0F };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0010, 0x45); // 0100 0101
		cpu->SetX(0x1);
		RunInst();
		// Shift left: 1000 1010
		EXPECT_EQ((uint8_t)0x8A, bus->read(0x0010));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestASLAbsolute)
	{
		uint8_t rom[] = { ASL_ABSOLUTE, 0x25, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1525, 0x45); // 0100 0101
		RunInst();
		// Shift left: 1000 1010
		EXPECT_EQ((uint8_t)0x8A, bus->read(0x1525));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestASLAbsoluteX)
	{
		uint8_t rom[] = { ASL_ABSOLUTE_X, 0x24, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1525, 0x45); // 0100 0101
		cpu->SetX(0x1);
		RunInst();
		// Shift left: 1000 1010
		EXPECT_EQ((uint8_t)0x8A, bus->read(0x1525));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_EQ(cpu->GetCycleCount(), 7);
	}

	TEST_F(MyEnv, TestBCCRelative)
	{
		uint8_t rom[] = { BCC_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_CARRY); // Clear carry to take branch
		RunInst();
		// After clocking BCC, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x8007, cpu->GetPC());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBCCRelativePageCross)
	{
		uint8_t rom[] = { BCC_RELATIVE, 0xDF, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_CARRY); // Clear carry to take branch
		RunInst();
		// After clocking BCC, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x7FE1, cpu->GetPC());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestBCCRelativeNotTaken)
	{
		uint8_t rom[] = { BCC_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_CARRY); // Set carry to not take branch
		RunInst();
		// After clocking BCC, PC should be at 0x8002 (start at 0x8000 + 2 for instruction)
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestBCSRelative)
	{
		uint8_t rom[] = { BCS_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_CARRY); // Set carry to take branch
		RunInst();
		// After clocking BCS, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x8007, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBCSRelativeNotTaken)
	{
		uint8_t rom[] = { BCS_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_CARRY); // Clear carry to not take branch
		RunInst();
		// After clocking BCS, PC should be at 0x8002 (start at 0x8000 + 2 for instruction)
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestBEQRelative)
	{
		uint8_t rom[] = { BEQ_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_ZERO); // Set zero to take branch
		RunInst();
		// After clocking BEQ, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x8007, cpu->GetPC());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_TRUE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBEQRelativeNotTaken)
	{
		uint8_t rom[] = { BEQ_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_ZERO); // Clear zero to not take branch
		RunInst();
		// After clocking BEQ, PC should be at 0x8002 (start at 0x8000 + 2 for instruction)
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestBITZeroPage)
	{
		uint8_t rom[] = { BIT_ZEROPAGE, 0x10 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0010, 0b11000000); // Set bits 6 and 7
		cpu->SetA(0b00000000); // A can be anything, just testing flags
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE)); // Bit 7 set
		EXPECT_TRUE(cpu->GetFlag(FLAG_OVERFLOW)); // Bit 6 set
		EXPECT_TRUE(cpu->GetFlag(FLAG_ZERO));    // A & M != 0
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBITAbsolute)
	{
		uint8_t rom[] = { BIT_ABSOLUTE, 0x10, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1510, 0b01000000); // Set bit 6
		cpu->SetA(0b11111111); // A can be anything, just testing flags
		RunInst();
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE)); // Bit 7 clear
		EXPECT_TRUE(cpu->GetFlag(FLAG_OVERFLOW));  // Bit 6 set
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));     // A & M != 0
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestBITZeroPageZeroResult)
	{
		uint8_t rom[] = { BIT_ZEROPAGE, 0x10 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0010, 0b00001111); // Lower nibble set
		cpu->SetA(0b11110000); // A upper nibble set, so A & M = 0
		RunInst();
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE)); // Bit 7 clear
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));  // Bit 6 clear
		EXPECT_TRUE(cpu->GetFlag(FLAG_ZERO));       // A & M == 0
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBITAbsoluteZeroResult)
	{
		uint8_t rom[] = { BIT_ABSOLUTE, 0x10, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1510, 0b00001111); // Lower nibble set
		cpu->SetA(0b11110000); // A upper nibble set, so A & M = 0
		RunInst();
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE)); // Bit 7 clear
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW)); // Bit 6 clear
		EXPECT_TRUE(cpu->GetFlag(FLAG_ZERO));       // A & M == 0 
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestBMIRelative)
	{
		uint8_t rom[] = { BMI_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_NEGATIVE); // Set negative to take branch
		RunInst();
		// After clocking BMI, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x8007, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBMIRelativeNotTaken)
	{
		uint8_t rom[] = { BMI_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_NEGATIVE); // Clear negative to not take branch
		RunInst();
		// After clocking BMI, PC should be at 0x8002 (start at 0x8000 + 2 for instruction)
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestBNERelative)
	{
		uint8_t rom[] = { BNE_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_ZERO); // Clear zero to take branch
		RunInst();
		// After clocking BNE, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x8007, cpu->GetPC());
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBNERelativeNotTaken)
	{
		uint8_t rom[] = { BNE_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_ZERO); // Set zero to not take branch
		RunInst();
		// After clocking BNE, PC should be at 0x8002 (start at 0x8000 + 2 for instruction)
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestBPLRelative)
	{
		uint8_t rom[] = { BPL_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_NEGATIVE); // Clear zero to take branch
		RunInst();
		// After clocking BNE, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x8007, cpu->GetPC());
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBPLRelativeNotTaken)
	{
		uint8_t rom[] = { BPL_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_NEGATIVE); // Set zero to not take branch
		RunInst();
		// After clocking BNE, PC should be at 0x8002 (start at 0x8000 + 2 for instruction)
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		EXPECT_TRUE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestBRKImplied)
	{
		// ROM needs to be large enough to store the reset vector.
		uint8_t rom[0x8000];
		rom[0] = BRK_IMPLIED;
		rom[1] = NOP_IMPLIED;
		rom[2] = NOP_IMPLIED;
		rom[3] = NOP_IMPLIED;
		uint8_t lo = 0x88;
		uint8_t hi = 0x80;
		rom[0xFFFE - 0x8000] = lo;
		rom[0xFFFF - 0x8000] = hi;
		cart->mapper->SetPRGRom(rom, sizeof(rom));

		cpu->ClearFlag(FLAG_INTERRUPT);
		cpu->SetPC(0x8000);
		RunInst();
		// After clocking BRK, PC should be at the IRQ vector address stored at 0xFFFE/F
		uint16_t irqVector = (hi << 8) | lo;
		EXPECT_EQ(irqVector, cpu->GetPC());
		// Also check that the Interrupt flag is set
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_EQ(cpu->GetCycleCount(), 7);
	}

	TEST_F(MyEnv, TestBVCRelative)
	{
		uint8_t rom[] = { BVC_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_OVERFLOW); // Clear overflow to take branch
		RunInst();
		// After clocking BVC, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x8007, cpu->GetPC());
		EXPECT_EQ(3, cpu->GetCycleCount());
	}

	TEST_F(MyEnv, TestBVCRelativeNotTaken)
	{
		uint8_t rom[] = { BVC_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_OVERFLOW); // Set overflow to not take branch
		RunInst();
		// After clocking BVC, PC should be at 0x8002 (start at 0x8000 + 2 for instruction)
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestBVSRelative)
	{
		uint8_t rom[] = { BVS_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_OVERFLOW); // Set overflow to take branch
		RunInst();
		// After clocking BVS, PC should be at 0x8007 (start at 0x8000 + 2 for instruction + 5 for branch)
		EXPECT_EQ((uint16_t)0x8007, cpu->GetPC());
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestBVSRelativeNotTaken)
	{
		uint8_t rom[] = { BVS_RELATIVE, 0x05, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->ClearFlag(FLAG_OVERFLOW); // Clear overflow to not take branch
		RunInst();
		// After clocking BVS, PC should be at 0x8002 (start at 0x8000 + 2 for instruction)
		EXPECT_EQ((uint16_t)0x8002, cpu->GetPC());
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestCLCImplied)
	{
		uint8_t rom[] = { CLC_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_CARRY); // Set carry flag
		RunInst();
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY)); // Carry flag should be cleared
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestCLDImplied)
	{
		uint8_t rom[] = { CLD_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_DECIMAL); // Set decimal flag
		RunInst();
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL)); // Decimal flag should be cleared
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestCLIImplied)
	{
		uint8_t rom[] = { CLI_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_INTERRUPT); // Set interrupt flag
		RunInst();
		EXPECT_FALSE(cpu->GetFlag(FLAG_INTERRUPT)); // Interrupt flag should be cleared
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestCLVImplied)
	{
		uint8_t rom[] = { CLV_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetFlag(FLAG_OVERFLOW); // Set overflow flag
		RunInst();
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW)); // Overflow flag should be cleared
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestCMPImmediate)
	{
		uint8_t rom[] = { CMP_IMMEDIATE, 0x30 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0x40);
		RunInst();
		// A (0x40) > M (0x30), so Carry should be set, Zero clear, Negative clear
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestCMPZeroPage)
	{
		// Add what is at zero page 0x15 to A.
		uint8_t rom[] = { CMP_ZEROPAGE, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x30);
		cpu->SetA(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestCMPZeroPageX)
	{
		// Add what is at zero page 0x15 to A.
		uint8_t rom[] = { CMP_ZEROPAGE_X, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0016, 0x30);
		cpu->SetX(0x1);
		cpu->SetA(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestCMPAbsolute)
	{
		uint8_t rom[] = { CMP_ABSOLUTE, 0x15, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x30);
		cpu->SetA(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestCMPAbsoluteX)
	{
		uint8_t rom[] = { CMP_ABSOLUTE_X, 0x14, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x30);
		cpu->SetX(0x1);
		cpu->SetA(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestCMPAbsoluteY)
	{
		uint8_t rom[] = { CMP_ABSOLUTE_Y, 0x14, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x30);
		cpu->SetY(0x1);
		cpu->SetA(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestCMPIndexedIndirect)
	{
		bus->write(0x0035, 0x35);
		bus->write(0x0036, 0x12); // Pointer to 0x1235
		bus->write(0x1235, 0x30);
		uint8_t rom[] = { CMP_INDEXEDINDIRECT, 0x33 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetX(0x2);
		cpu->SetA(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestCMPIndirectIndexed)
	{
		bus->write(0x0035, 0x35);
		bus->write(0x0036, 0x12); // Pointer to 0x1235
		bus->write(0x1237, 0x30);
		uint8_t rom[] = { CMP_INDIRECTINDEXED, 0x35 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetY(0x2);
		cpu->SetA(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 5);
	}

	TEST_F(MyEnv, TestCPXImmediate)
	{
		uint8_t rom[] = { CPX_IMMEDIATE, 0x30 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetX(0x40);
		RunInst();
		// X (0x40) > M (0x30), so Carry should be set, Zero clear, Negative clear
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestCPXZeroPage)
	{
		uint8_t rom[] = { CPX_ZEROPAGE, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x30);
		cpu->SetX(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestCPXAbsolute)
	{
		uint8_t rom[] = { CPX_ABSOLUTE, 0x15, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x30);
		cpu->SetX(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestCPYImmediate)
	{
		uint8_t rom[] = { CPY_IMMEDIATE, 0x30 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetY(0x40);
		RunInst();
		// Y (0x40) > M (0x30), so Carry should be set, Zero clear, Negative clear
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestCPYZeroPage)
	{
		uint8_t rom[] = { CPY_ZEROPAGE, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x30);
		cpu->SetY(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestCPYAbsolute)
	{
		uint8_t rom[] = { CPY_ABSOLUTE, 0x15, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x30);
		cpu->SetY(0x40);
		RunInst();
		EXPECT_TRUE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestDECZeroPage)
	{
		uint8_t rom[] = { DEC_ZEROPAGE, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x30);
		RunInst();
		EXPECT_EQ(bus->read(0x0015), 0x2F);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 5);
	}

	TEST_F(MyEnv, TestDECZeroPageZeroResult)
	{
		uint8_t rom[] = { DEC_ZEROPAGE, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x01);
		RunInst();
		EXPECT_EQ(bus->read(0x0015), 0x00);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_EQ(cpu->GetCycleCount(), 5);
	}

	TEST_F(MyEnv, TestDECZeroPageX)
	{
		uint8_t rom[] = { DEC_ZEROPAGE_X, 0x14 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x30);
		cpu->SetX(0x1);
		RunInst();
		EXPECT_EQ(bus->read(0x0015), 0x2F);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestDECAbsolute)
	{
		uint8_t rom[] = { DEC_ABSOLUTE, 0x15, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x30);
		RunInst();
		EXPECT_EQ(bus->read(0x1215), 0x2F);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestDECAbsoluteX)
	{
		uint8_t rom[] = { DEC_ABSOLUTE_X, 0x14, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x30);
		cpu->SetX(0x1);
		RunInst();
		EXPECT_EQ(bus->read(0x1215), 0x2F);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_EQ(cpu->GetCycleCount(), 7);
	}

	TEST_F(MyEnv, TestDEXImplied)
	{
		uint8_t rom[] = { DEX_IMPLIED  };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetX(0x01);
		RunInst();
		EXPECT_EQ((uint8_t)0x00, cpu->GetX());
		EXPECT_TRUE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestDEYImplied)
	{
		uint8_t rom[] = { DEY_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetY(0x01);
		RunInst();
		EXPECT_EQ((uint8_t)0x00, cpu->GetY());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_TRUE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestEORImmediate)
	{
		uint8_t rom[] = { EOR_IMMEDIATE, 0xAA }; // 1010 1010
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetA(0xFF);
		RunInst();
		// Y (0x40) > M (0x30), so Carry should be set, Zero clear, Negative clear
		EXPECT_EQ((uint8_t)(0xFF ^ 0xAA), cpu->GetA()); // 0101 0101
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestEORZeroPage)
	{
		uint8_t rom[] = { EOR_ZEROPAGE, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0xAA); // 1010 1010
		cpu->SetA(0xFF);
		RunInst();
		EXPECT_EQ((uint8_t)(0xFF ^ 0xAA), cpu->GetA()); // 0101 0101
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 3);
	}

	TEST_F(MyEnv, TestEORZeroPageX)
	{
		uint8_t rom[] = { EOR_ZEROPAGE_X, 0x14 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0xAA); // 1010 1010
		cpu->SetX(0x1);
		cpu->SetA(0xFF);
		RunInst();
		EXPECT_EQ((uint8_t)(0xFF ^ 0xAA), cpu->GetA()); // 0101 0101
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestEORAbsolute)
	{
		uint8_t rom[] = { EOR_ABSOLUTE, 0x15, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0xAA); // 1010 1010
		cpu->SetA(0xFF);
		RunInst();
		EXPECT_EQ((uint8_t)(0xFF ^ 0xAA), cpu->GetA()); // 0101 0101
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestEORAbsoluteX)
	{
		uint8_t rom[] = { EOR_ABSOLUTE_X, 0x14, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0xAA); // 1010 1010
		cpu->SetX(0x1);
		cpu->SetA(0xFF);
		RunInst();
		EXPECT_EQ((uint8_t)(0xFF ^ 0xAA), cpu->GetA()); // 0101 0101
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestEORAbsoluteY)
	{
		uint8_t rom[] = { EOR_ABSOLUTE_Y, 0x14, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0xAA); // 1010 1010
		cpu->SetY(0x1);
		cpu->SetA(0xFF);
		RunInst();
		EXPECT_EQ((uint8_t)(0xFF ^ 0xAA), cpu->GetA()); // 0101 0101
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 4);
	}

	TEST_F(MyEnv, TestEORIndexedIndirect)
	{
		bus->write(0x0035, 0x35);
		bus->write(0x0036, 0x12); // Pointer to 0x1235
		bus->write(0x1235, 0xAA); // 1010 1010
		uint8_t rom[] = { EOR_INDEXEDINDIRECT, 0x33 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetX(0x2);
		cpu->SetA(0xFF);
		RunInst();
		EXPECT_EQ((uint8_t)(0xFF ^ 0xAA), cpu->GetA()); // 0101 0101
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestEORIndirectIndexed)
	{
		bus->write(0x0035, 0x35);
		bus->write(0x0036, 0x12); // Pointer to 0x1235
		bus->write(0x1237, 0xAA); // 1010 1010
		uint8_t rom[] = { EOR_INDIRECTINDEXED, 0x35 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetY(0x2);
		cpu->SetA(0xFF);
		RunInst();
		EXPECT_EQ((uint8_t)(0xFF ^ 0xAA), cpu->GetA()); // 0101 0101
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 5);
	}

	TEST_F(MyEnv, TestINCZeroPage)
	{
		uint8_t rom[] = { INC_ZEROPAGE, 0x15 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x2A);
		RunInst();
		EXPECT_EQ(bus->read(0x0015), 0x2B);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 5);
	}

	TEST_F(MyEnv, TestINCZeroPageX)
	{
		uint8_t rom[] = { INC_ZEROPAGE_X, 0x14 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x0015, 0x2A);
		cpu->SetX(0x1);
		RunInst();
		EXPECT_EQ(bus->read(0x0015), 0x2B);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestINCAbsolute)
	{
		uint8_t rom[] = { INC_ABSOLUTE, 0x15, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x2A);
		RunInst();
		EXPECT_EQ(bus->read(0x1215), 0x2B);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 6);
	}

	TEST_F(MyEnv, TestINCAbsoluteX)
	{
		uint8_t rom[] = { INC_ABSOLUTE_X, 0x14, 0x12 };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		bus->write(0x1215, 0x2A);
		cpu->SetX(0x1);
		RunInst();
		EXPECT_EQ(bus->read(0x1215), 0x2B);
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 7);
	}

	TEST_F(MyEnv, TestINXImplied)
	{
		uint8_t rom[] = { INX_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetX(0x01);
		RunInst();
		EXPECT_EQ((uint8_t)0x02, cpu->GetX());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

	TEST_F(MyEnv, TestINYImplied)
	{
		uint8_t rom[] = { INY_IMPLIED };
		cart->mapper->SetPRGRom(rom, sizeof(rom));
		cpu->SetY(0x01);
		RunInst();
		EXPECT_EQ((uint8_t)0x02, cpu->GetY());
		EXPECT_FALSE(cpu->GetFlag(FLAG_CARRY));
		EXPECT_FALSE(cpu->GetFlag(FLAG_DECIMAL));
		EXPECT_TRUE(cpu->GetFlag(FLAG_INTERRUPT));
		EXPECT_FALSE(cpu->GetFlag(FLAG_OVERFLOW));
		EXPECT_TRUE(cpu->GetFlag(FLAG_UNUSED));
		EXPECT_FALSE(cpu->GetFlag(FLAG_ZERO));
		EXPECT_FALSE(cpu->GetFlag(FLAG_NEGATIVE));
		EXPECT_EQ(cpu->GetCycleCount(), 2);
	}

    int main(int argc, char** argv)
    {
        ::testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
    }

}
//#include <cstdlib>
//#include "pch.h"
//#include "CppUnitTest.h"

//
//#include "INESLoader.h"
//

//

//
//		TEST_F(MyEnv, TestJMPAbsolute)
//		{
//			uint8_t rom[] = { JMP_ABSOLUTE, 0x00, 0x90 }; // Jump to 0x9000
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			RunInst();
//			Assert::AreEqual((uint16_t)0x9000, cpu->GetPC());
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestJMPIndirect)
//		{
//			bus->write(0x10FF, 0x00); // Low byte of jump address
//			bus->write(0x1000, 0x90); // High byte of jump address (note the page boundary wraparound)
//			uint8_t rom[] = { JMP_INDIRECT, 0xFF, 0x10 }; // Pointer at 0x30FF/3000
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			RunInst();
//			Assert::AreEqual((uint16_t)0x9000, cpu->GetPC());
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//		TEST_F(MyEnv, TestJMPIndirectNoBug)
//		{
//			bus->write(0x10F0, 0x00); // Low byte of jump address
//			bus->write(0x10F1, 0x90); // High byte of jump address
//			uint8_t rom[] = { JMP_INDIRECT, 0xF0, 0x10 }; // Pointer at 0x30FF/3000
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			RunInst();
//			Assert::AreEqual((uint16_t)0x9000, cpu->GetPC());
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//
//		TEST_F(MyEnv, TestJSRAbsolute)
//		{
//			uint8_t rom[] = { JSR_ABSOLUTE, 0x05, 0x80, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED, NOP_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetPC(0x8000);
//			RunInst();
//			// After clocking JSR, PC should be at 0x8005
//			Assert::AreEqual((uint16_t)0x8005, cpu->GetPC());
//			// The return address (0x8002) should be on the stack
//			uint8_t lo = bus->read(0x0100 + cpu->GetSP() + 1);
//			uint8_t hi = bus->read(0x0100 + cpu->GetSP() + 2);
//			uint16_t returnAddress = (hi << 8) | lo;
//			Assert::AreEqual((uint16_t)0x8002, returnAddress);
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//
//		TEST_F(MyEnv, TestLDAAbsolute)
//		{
//			uint8_t rom[] = { LDA_ABSOLUTE, 0x10, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1510, 0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestLDAAbsoluteX)
//		{
//			uint8_t rom[] = { LDA_ABSOLUTE_X, 0x0F, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetFlag(FLAG_ZERO); // Set zero flag to see if it gets cleared
//			cpu->SetFlag(FLAG_NEGATIVE); // Set negative flag to see if it gets cleared
//			bus->write(0x1510, 0x37);
//			cpu->SetX(0x1);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//
//		TEST_F(MyEnv, TestLDAAbsoluteXPageCross)
//		{
//			uint8_t rom[] = { LDA_ABSOLUTE_X, 0x0F, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetFlag(FLAG_ZERO); // Set zero flag to see if it gets cleared
//			cpu->SetFlag(FLAG_NEGATIVE); // Set negative flag to see if it gets cleared
//			bus->write(0x160E, 0x37);
//			cpu->SetX(0xFF);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//		TEST_F(MyEnv, TestLDAAbsoluteY)
//		{
//			uint8_t rom[] = { LDA_ABSOLUTE_Y, 0x0F, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1510, 0x37);
//			cpu->SetY(0x1);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestLDAImmediate)
//		{
//			uint8_t rom[] = { LDA_IMMEDIATE, 0x42 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			RunInst();
//			Assert::AreEqual((uint8_t)0x42, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestLDAIndexedIndirect)
//		{
//			bus->write(0x0020, 0x40);
//			bus->write(0x0021, 0x12); // Pointer to 0x1240
//			bus->write(0x1240, 0x37);
//			uint8_t rom[] = { LDA_INDEXEDINDIRECT, 0x1C };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x4);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestLDAIndirectIndexed)
//		{
//			bus->write(0x0020, 0x40);
//			bus->write(0x0021, 0x12); // Pointer to 0x1240
//			bus->write(0x1242, 0x37);
//			uint8_t rom[] = { LDA_INDIRECTINDEXED, 0x20 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x2);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//		TEST_F(MyEnv, TestLDAZeroPage)
//		{
//			uint8_t rom[] = { LDA_ZEROPAGE, 0x10 };
//			cpu->SetFlag(FLAG_ZERO); // Set zero flag to see if it gets cleared
//			cpu->SetFlag(FLAG_NEGATIVE); // Set negative flag to see if it gets cleared
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0010, 0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestLDAZeroPageX)
//		{
//			uint8_t rom[] = { LDA_ZEROPAGE_X, 0x10 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x37);
//			cpu->SetX(0x5);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//
//		TEST_F(MyEnv, TestLDXImmediate)
//		{
//			uint8_t rom[] = { LDX_IMMEDIATE, 0x55  };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			RunInst();
//			Assert::AreEqual((uint8_t)0x55, cpu->GetX());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestLDXZeroPage)
//		{
//			uint8_t rom[] = { LDX_ZEROPAGE, 0x20 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0020, 0x66);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x66, cpu->GetX());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestLDXZeroPageY)
//		{
//			uint8_t rom[] = { LDX_ZEROPAGE_Y, 0x1F };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0024, 0x66);
//			cpu->SetY(0x5);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x66, cpu->GetX());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestLDXAbsolute)
//		{
//			uint8_t rom[] = { LDX_ABSOLUTE, 0x30, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1530, 0x77);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetX());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestLDXAbsoluteY)
//		{
//			uint8_t rom[] = { LDX_ABSOLUTE_Y, 0x2F, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1530, 0x77);
//			cpu->SetY(0x1);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetX());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestLDYImmediate)
//		{
//			uint8_t rom[] = { LDY_IMMEDIATE, 0x55 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			RunInst();
//			Assert::AreEqual((uint8_t)0x55, cpu->GetY());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestLDYZeroPage)
//		{
//			uint8_t rom[] = { LDY_ZEROPAGE, 0x20 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0020, 0x66);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x66, cpu->GetY());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestLDYZeroPageX)
//		{
//			uint8_t rom[] = { LDY_ZEROPAGE_X, 0x1F };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0024, 0x66);
//			cpu->SetX(0x5);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x66, cpu->GetY());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestLDYAbsolute)
//		{
//			uint8_t rom[] = { LDY_ABSOLUTE, 0x30, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1530, 0x77);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetY());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestLDYAbsoluteX)
//		{
//			uint8_t rom[] = { LDY_ABSOLUTE_X, 0x2F, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1530, 0x77);
//			cpu->SetX(0x1);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetY());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//
//		TEST_F(MyEnv, TestLSRAccumulator)
//		{
//			uint8_t rom[] = { LSR_ACCUMULATOR };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x02); // 0000 0010
//			RunInst();
//			Assert::AreEqual((uint8_t)0x01, cpu->GetA()); // 0000 0001
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsFalse(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestLSRZeroPage)
//		{
//			uint8_t rom[] = { LSR_ZEROPAGE, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x02); // 0000 0010
//			RunInst();
//			Assert::AreEqual((uint8_t)0x01, bus->read(0x0015)); // 0000 0001
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsFalse(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//		TEST_F(MyEnv, TestLSRZeroPageX)
//		{
//			uint8_t rom[] = { LSR_ZEROPAGE_X, 0x14 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x02); // 0000 0010
//			cpu->SetX(0x1);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x01, bus->read(0x0015)); // 0000 0001
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsFalse(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestLSRAbsolute)
//		{
//			uint8_t rom[] = { LSR_ABSOLUTE, 0x15, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x02); // 0000 0010
//			RunInst();
//			Assert::AreEqual((uint8_t)0x01, bus->read(0x1215)); // 0000 0001
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsFalse(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestLSRAbsoluteX)
//		{
//			uint8_t rom[] = { LSR_ABSOLUTE_X, 0x14, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x02); // 0000 0010
//			cpu->SetX(0x1);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x01, bus->read(0x1215)); // 0000 0001
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsFalse(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetCycleCount() == 7);
//		}
//
//		TEST_F(MyEnv, TestNOPImmediate)
//		{
//			uint8_t rom[] = { NOP_IMPLIED, 0x00 }; // 0000 1111
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			RunInst();
//			// A (0xF0) | M (0x0F) = 0xFF
//			Assert::AreEqual((uint16_t)0x8001, cpu->GetPC()); // 1111 1111
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//
//		TEST_F(MyEnv, TestORAImmediate)
//		{
//			uint8_t rom[] = { ORA_IMMEDIATE, 0x0F }; // 0000 1111
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0xF0); // 1111 0000
//			RunInst();
//			// A (0xF0) | M (0x0F) = 0xFF
//			Assert::AreEqual((uint8_t)(0xF0 | 0x0F), cpu->GetA()); // 1111 1111
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsTrue(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestORAZeroPage)
//		{
//			uint8_t rom[] = { ORA_ZEROPAGE, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x0F); // 0000 1111
//			cpu->SetA(0xF0); // 1111 0000
//			RunInst();
//			Assert::AreEqual((uint8_t)(0xF0 | 0x0F), cpu->GetA()); // 1111 1111
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsTrue(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestORAZeroPageX)
//		{
//			uint8_t rom[] = { ORA_ZEROPAGE_X, 0x14 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x0F); // 0000 1111
//			cpu->SetX(0x1);
//			cpu->SetA(0xF0); // 1111 0000
//			RunInst();
//			Assert::AreEqual((uint8_t)(0xF0 | 0x0F), cpu->GetA()); // 1111 1111
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsTrue(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestORAAbsolute)
//		{
//			uint8_t rom[] = { ORA_ABSOLUTE, 0x15, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x0F); // 0000 1111
//			cpu->SetA(0xF0); // 1111 0000
//			RunInst();
//			Assert::AreEqual((uint8_t)(0xF0 | 0x0F), cpu->GetA()); // 1111 1111
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsTrue(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestORAAbsoluteX)
//		{
//			uint8_t rom[] = { ORA_ABSOLUTE_X, 0x14, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x0F); // 0000 1111
//			cpu->SetX(0x1);
//			cpu->SetA(0xF0); // 1111 0000
//			RunInst();
//			Assert::AreEqual((uint8_t)(0xF0 | 0x0F), cpu->GetA()); // 1111 1111
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsTrue(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestORAAbsoluteY)
//		{
//			uint8_t rom[] = { ORA_ABSOLUTE_Y, 0x14, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x0F); // 0000 1111
//			cpu->SetY(0x1);
//			cpu->SetA(0xF0); // 1111 0000
//			RunInst();
//			Assert::AreEqual((uint8_t)(0xF0 | 0x0F), cpu->GetA()); // 1111 1111
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsTrue(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestORAIndexedIndirect)
//		{
//			bus->write(0x0035, 0x35);
//			bus->write(0x0036, 0x12); // Pointer to 0x1235
//			bus->write(0x1235, 0x0F); // 0000 1111
//			uint8_t rom[] = { ORA_INDEXEDINDIRECT, 0x33 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x2);
//			cpu->SetA(0xF0); // 1111 0000
//			RunInst();
//			Assert::AreEqual((uint8_t)(0xF0 | 0x0F), cpu->GetA()); // 1111 1111
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsTrue(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestORAIndirectIndexed)
//		{
//			bus->write(0x0035, 0x35);
//			bus->write(0x0036, 0x12); // Pointer to 0x1235
//			bus->write(0x1237, 0x0F); // 0000 1111
//			uint8_t rom[] = { ORA_INDIRECTINDEXED, 0x35 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x2);
//			cpu->SetA(0xF0); // 1111 0000
//			RunInst();
//			Assert::AreEqual((uint8_t)(0xF0 | 0x0F), cpu->GetA()); // 1111 1111
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsTrue(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//
//		TEST_F(MyEnv, TestRTIImplied)
//		{
//			uint8_t rom[] = { RTI_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			// Push status and return address onto stack
//			bus->write(0x01FF, 0x80); // Return address high byte
//			bus->write(0x01FE, 0x00); // Return address low byte
//			bus->write(0x01FD, 0x24); // Status with some flags set
//			cpu->SetSP(0xFC); // Set SP to point to 0x01FD
//			RunInst();
//			Assert::AreEqual((uint16_t)0x8000, cpu->GetPC());
//			Assert::AreEqual((uint8_t)0x24, cpu->GetStatus());
//			Assert::AreEqual((uint8_t)0xFF, cpu->GetSP());
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//
//		TEST_F(MyEnv, TestPHAImplied)
//		{
//			uint8_t rom[] = { PHA_IMPLIED  };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x42);
//			uint8_t initialSP = cpu->GetSP();
//			RunInst();
//			uint8_t valueOnStack = bus->read(0x0100 + initialSP);
//			Assert::AreEqual((uint8_t)0x42, valueOnStack);
//			Assert::AreEqual((uint8_t)(initialSP - 1), cpu->GetSP());
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestPHPImplied)
//		{
//			uint8_t rom[] = { PHP_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetStatus(0b10100000); // Set some flags
//			uint8_t initialSP = cpu->GetSP();
//			RunInst();
//			uint8_t valueOnStack = bus->read(0x0100 + initialSP);
//			Assert::AreEqual((uint8_t)(0b10100000 | 0b00110000), valueOnStack); // Break and Unused bits should be set
//			Assert::AreEqual((uint8_t)(initialSP - 1), cpu->GetSP());
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestPLAImplied)
//		{
//			uint8_t rom[] = { PLA_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x01FF, 0x37); // Value to pull from stack
//			cpu->SetSP(0xFE); // Set SP to point to 0x01FF
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, cpu->GetA());
//			Assert::AreEqual((uint8_t)0xFF, cpu->GetSP());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestPLPImplied)
//		{
//			uint8_t rom[] = { PLP_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			uint8_t stack = FLAG_BREAK | FLAG_UNUSED | FLAG_CARRY | FLAG_ZERO; // 0b11000101
//			bus->write(0x01FF, stack); // Value to pull from stack
//			cpu->SetSP(0xFE); // Set SP to point to 0x01FF
//			RunInst();
//			stack = stack & ~(FLAG_BREAK); // Clear Break bits for comparison
//			// The Unused flag stays since it was set on power on.
//			Assert::AreEqual(stack, cpu->GetStatus()); // Break and Unused bits should be ignored
//			Assert::AreEqual((uint8_t)0xFF, cpu->GetSP());
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//
//		TEST_F(MyEnv, TestROLAccumulator)
//		{
//			uint8_t rom[] = { ROL_ACCUMULATOR };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x80); // 1000 0000
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, cpu->GetA()); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestROLZeroPage)
//		{
//			uint8_t rom[] = { ROL_ZEROPAGE, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x80); // 1000 0000
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, bus->read(0x0015)); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//		TEST_F(MyEnv, TestROLZeroPageX)
//		{
//			uint8_t rom[] = { ROL_ZEROPAGE_X, 0x14 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x80); // 1000 0000
//			cpu->SetX(0x1);
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, bus->read(0x0015)); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestROLAbsolute)
//		{
//			uint8_t rom[] = { ROL_ABSOLUTE, 0x15, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x80); // 1000 0000
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, bus->read(0x1215)); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestROLAbsoluteX)
//		{
//			uint8_t rom[] = { ROL_ABSOLUTE_X, 0x14, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x80); // 1000 0000
//			cpu->SetX(0x1);
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, bus->read(0x1215)); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 7);
//		}
//
//		TEST_F(MyEnv, TestRORAccumulator)
//		{
//			uint8_t rom[] = { ROR_ACCUMULATOR };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x01); // 0000 0001
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, cpu->GetA()); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestRORZeroPage)
//		{
//			uint8_t rom[] = { ROR_ZEROPAGE, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x01); // 0000 0001
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, bus->read(0x0015)); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//		TEST_F(MyEnv, TestRORZeroPageX)
//		{
//			uint8_t rom[] = { ROR_ZEROPAGE_X, 0x14 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0015, 0x01); // 0000 0001
//			cpu->SetX(0x1);
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, bus->read(0x0015)); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestRORAbsolute)
//		{
//			uint8_t rom[] = { ROR_ABSOLUTE, 0x15, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x01); // 0000 0001
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, bus->read(0x1215)); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestRORAbsoluteX)
//		{
//			uint8_t rom[] = { ROR_ABSOLUTE_X, 0x14, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1215, 0x01); // 0000 0001
//			cpu->SetX(0x1);
//			cpu->ClearFlag(FLAG_CARRY);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x00, bus->read(0x1215)); // 0000 0000
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 7);
//		}
//
//		TEST_F(MyEnv, TestRTSImplied)
//		{
//			uint8_t rom[] = { RTS_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			// Push return address onto stack
//			bus->write(0x01FF, 0x80); // Return address high byte
//			bus->write(0x01FE, 0x05); // Return address low byte
//			cpu->SetSP(0xFD); // Set SP to point to 0x01FE
//			RunInst();
//			Assert::AreEqual((uint16_t)0x8006, cpu->GetPC()); // PC should be return address + 1
//			Assert::AreEqual((uint8_t)0xFF, cpu->GetSP());
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//
//		TEST_F(MyEnv, TestSBCImmediate)
//		{
//			uint8_t rom[] = { SBC_IMMEDIATE, 0x10 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x20);
//			cpu->SetFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::AreEqual((uint8_t)0x10, cpu->GetA());
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_OVERFLOW));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestSBCZeroPage)
//		{
//			uint8_t rom[] = { SBC_ZEROPAGE, 0x30 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0030, 0x10);
//			cpu->SetA(0x20);
//			cpu->SetFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::AreEqual((uint8_t)0x10, cpu->GetA());
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_OVERFLOW));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestSBCZeroPageX)
//		{
//			uint8_t rom[] = { SBC_ZEROPAGE_X, 0x2F };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x0030, 0x10);
//			cpu->SetX(0x1);
//			cpu->SetA(0x20);
//			cpu->SetFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::AreEqual((uint8_t)0x10, cpu->GetA());
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_OVERFLOW));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestSBCAbsolute)
//		{
//			uint8_t rom[] = { SBC_ABSOLUTE, 0x40, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1240, 0x10);
//			cpu->SetA(0x20);
//			cpu->SetFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::AreEqual((uint8_t)0x10, cpu->GetA());
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_OVERFLOW));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestSBCAbsoluteX)
//		{
//			uint8_t rom[] = { SBC_ABSOLUTE_X, 0x3F, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1240, 0x10);
//			cpu->SetX(0x1);
//			cpu->SetA(0x20);
//			cpu->SetFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::AreEqual((uint8_t)0x10, cpu->GetA());
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_OVERFLOW));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestSBCAbsoluteY)
//		{
//			uint8_t rom[] = { SBC_ABSOLUTE_Y, 0x3F, 0x12 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			bus->write(0x1240, 0x10);
//			cpu->SetY(0x1);
//			cpu->SetA(0x20);
//			cpu->SetFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::AreEqual((uint8_t)0x10, cpu->GetA());
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_OVERFLOW));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestSBCIndexedIndirect)
//		{
//			bus->write(0x0040, 0x50);
//			bus->write(0x0041, 0x12); // Pointer to 0x1250
//			bus->write(0x1250, 0x10);
//			uint8_t rom[] = { SBC_INDEXEDINDIRECT, 0x3C };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x4);
//			cpu->SetA(0x20);
//			cpu->SetFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::AreEqual((uint8_t)0x10, cpu->GetA());
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_OVERFLOW));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestSBCIndirectIndexed)
//		{
//			bus->write(0x0040, 0x50);
//			bus->write(0x0041, 0x12); // Pointer to 0x1250
//			bus->write(0x1252, 0x10);
//			uint8_t rom[] = { SBC_INDIRECTINDEXED, 0x40 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x2);
//			cpu->SetA(0x20);
//			cpu->SetFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::AreEqual((uint8_t)0x10, cpu->GetA());
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_OVERFLOW));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//
//		TEST_F(MyEnv, TestSECImplied)
//		{
//			uint8_t rom[] = { SEC_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->ClearFlag(FLAG_CARRY); // Set carry for no borrow
//			RunInst();
//			Assert::IsTrue(cpu->GetFlag(FLAG_CARRY));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//
//		TEST_F(MyEnv, TestSEDImplied)
//		{
//			uint8_t rom[] = { SED_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->ClearFlag(FLAG_DECIMAL); // Clear decimal flag
//			RunInst();
//			Assert::IsTrue(cpu->GetFlag(FLAG_DECIMAL));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//
//		TEST_F(MyEnv, TestSEIImplied)
//		{
//			uint8_t rom[] = { SEI_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->ClearFlag(FLAG_INTERRUPT); // Clear interrupt disable flag
//			RunInst();
//			Assert::IsTrue(cpu->GetFlag(FLAG_INTERRUPT));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//
//		TEST_F(MyEnv, TestSTAAbsolute)
//		{
//			uint8_t rom[] = { STA_ABSOLUTE, 0x20, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x1520));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestSTAAbsoluteX)
//		{
//			uint8_t rom[] = { STA_ABSOLUTE_X, 0x1F, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x1);
//			cpu->SetA(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x1520));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//		TEST_F(MyEnv, TestSTAAbsoluteY)
//		{
//			uint8_t rom[] = { STA_ABSOLUTE_Y, 0x1F, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x1);
//			cpu->SetA(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x1520));
//			Assert::IsTrue(cpu->GetCycleCount() == 5);
//		}
//		TEST_F(MyEnv, TestSTAIndexedIndirect)
//		{
//			bus->write(0x0040, 0x30);
//			bus->write(0x0041, 0x12); // Pointer to 0x1230
//			uint8_t rom[] = { STA_INDEXEDINDIRECT, 0x3E };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x2);
//			cpu->SetA(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x1230));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestSTAIndirectIndexed)
//		{
//			bus->write(0x0040, 0x30);
//			bus->write(0x0041, 0x12); // Pointer to 0x1230
//			uint8_t rom[] = { STA_INDIRECTINDEXED, 0x40 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x2);
//			cpu->SetA(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x1232));
//			Assert::IsTrue(cpu->GetCycleCount() == 6);
//		}
//		TEST_F(MyEnv, TestSTAZeroPage)
//		{
//			uint8_t rom[] = { STA_ZEROPAGE, 0x10 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x0010));
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestSTAZeroPageX)
//		{
//			uint8_t rom[] = { STA_ZEROPAGE_X, 0x0F };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x1);
//			cpu->SetA(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x0010));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//
//		TEST_F(MyEnv, TestSTXZeroPage)
//		{
//			uint8_t rom[] = { STX_ZEROPAGE, 0x10 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x0010));
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestSTXZeroPageY)
//		{
//			uint8_t rom[] = { STX_ZEROPAGE_Y, 0x0F };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x1);
//			cpu->SetX(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x0010));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestSTXAbsolute)
//		{
//			uint8_t rom[] = { STX_ABSOLUTE, 0x20, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x1520));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//
//		TEST_F(MyEnv, TestSTYZeroPage)
//		{
//			uint8_t rom[] = { STY_ZEROPAGE, 0x10 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x0010));
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//		TEST_F(MyEnv, TestSTYZeroPageX)
//		{
//			uint8_t rom[] = { STY_ZEROPAGE_X, 0x0F };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x1);
//			cpu->SetY(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x0010));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//		TEST_F(MyEnv, TestSTYAbsolute)
//		{
//			uint8_t rom[] = { STY_ABSOLUTE, 0x20, 0x15 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x37);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x37, bus->read(0x1520));
//			Assert::IsTrue(cpu->GetCycleCount() == 4);
//		}
//
//		TEST_F(MyEnv, TestTAXImplied)
//		{
//			uint8_t rom[] = { TAX_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x77);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetX());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestTAYImplied)
//		{
//			uint8_t rom[] = { TAY_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetA(0x77);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetY());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestTSXImplied)
//		{
//			uint8_t rom[] = { TSX_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetSP(0x77);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetX());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestTXAImplied)
//		{
//			uint8_t rom[] = { TXA_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x77);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestTXSImplied)
//		{
//			uint8_t rom[] = { TXS_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetX(0x77);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetSP());
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//		TEST_F(MyEnv, TestTYAImplied)
//		{
//			uint8_t rom[] = { TYA_IMPLIED };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			cpu->SetY(0x77);
//			cpu->SetA(0x05);
//			RunInst();
//			Assert::AreEqual((uint8_t)0x77, cpu->GetA());
//			Assert::IsFalse(cpu->GetFlag(FLAG_ZERO));
//			Assert::IsFalse(cpu->GetFlag(FLAG_NEGATIVE));
//			Assert::IsTrue(cpu->GetCycleCount() == 2);
//		}
//
//		// Unofficial op codes
//		TEST_F(MyEnv, TestNOPZP04)
//		{
//			uint8_t rom[] = { 0x04, 0x00 };
//			cart->mapper->SetPRGRom(rom, sizeof(rom));
//			RunInst();
//			Assert::AreEqual((uint16_t)0x8002, cpu->GetPC());
//			Assert::IsTrue(cpu->GetCycleCount() == 3);
//		}
//	};
//}