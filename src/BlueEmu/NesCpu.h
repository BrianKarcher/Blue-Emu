#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <functional>

class DebuggerContext;

#ifdef _DEBUG
#define LOG(...) dbg(__VA_ARGS__)
#else
#define LOG(...) do {} while(0) // completely removed by compiler
#endif

#ifdef _DEBUG
#define LOG_NMI(...) dbgNmi(__VA_ARGS__)
#else
#define LOG_NMI(...) do {} while(0) // completely removed by compiler
#endif

//#define CPUDEBUG
//#define NMIDEBUG

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_UNUSED    0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_NEGATIVE  0x80

class Bus;
class OpenBusMapper;
class Serializer;
class SharedContext;
class PPU;

class NesCpu
{
public:
	NesCpu(OpenBusMapper& openBus, SharedContext& ctx, DebuggerContext& dbg, PPU& ppu);
	void connectBus(Bus* bus);

	bool ShouldPause();
	inline uint8_t ReadByte(uint16_t addr);
	void WriteByte(uint16_t addr, uint8_t value);

	void SetNMIImmediate();
	void setNMI(bool state);
	void SetIRQImmediate();
	void setIRQ(bool state);
	// Power Cycle and Reset are different
	void PowerCycle();
	uint8_t Clock();
	uint8_t GetA();
	void SetA(uint8_t a);
	uint8_t GetX();
	void SetX(uint8_t x);
	uint8_t GetY();
	void SetY(uint8_t y);
	bool GetFlag(uint8_t flag);
	void SetFlag(uint8_t flag);
	void Reset();
	void AddCycles(int count);
	uint64_t GetCycleCount();
	Bus* bus;
	void Activate(bool active);
	void SetPC(uint16_t address);
	uint16_t GetPC();
	uint8_t GetStatus();
	void SetStatus(uint8_t status);
	void ClearFlag(uint8_t flag);
	uint8_t GetSP();
	void SetSP(uint8_t sp);
	int cyclesThisFrame;
	void setFrozen(bool frozen) { isFrozen = frozen; }
	void toggleFrozen() { isFrozen = !isFrozen; }
	void ConsumeCycle();

	// Registers
	uint8_t m_a;
	uint8_t m_x;
	uint8_t m_y;
	uint8_t _operand;

	// Internal Latch Registers (to hold state between cycles)
	// We widen this to 16-bit to hold 0x100 (NMI) and 0x101 (IRQ)
	uint16_t  current_opcode;
	uint8_t  addr_low;
	uint8_t  addr_high;
	uint8_t  m_temp_low;
	uint16_t effective_addr;
	int8_t offset;
	bool inst_complete = true;
	bool addr_complete = false;

	// Emulator State
	int cycle_state;  // Tracks the current micro-op (0, 1, 2...)
	bool page_crossed;

	// --- Constants for Phantom Opcodes ---
	static constexpr uint16_t OP_NMI = 0x100;
	static constexpr uint16_t OP_IRQ = 0x101;
	static constexpr uint16_t OP_RESET = 0x102;

	// The CPU Tick Loop
	void cpu_tick();
	// Helper to update Zero and Negative flags
	void update_ZN_flags(uint8_t value) {
		if (value == 0) m_p |= 0x02; else m_p &= ~0x02; // Zero Flag
		if (value & 0x80) m_p |= 0x80; else m_p &= ~0x80; // Negative Flag
	}

	void init_cpu();

	void Serialize(Serializer& serializer);
	void Deserialize(Serializer& serializer);
private:
	// The effective pattern we are following here is we advance one cycle per read/write to the bus
	// We return false on each cycle until each process is complete.
	// The last cycle in "Mode" returns true on an incomplete cycle
	// with no bus access, so Op runs immediately the same cycle
	// to access the bus and complete the cycle.

	struct Mode_Immediate {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
				cpu.effective_addr = cpu.m_pc++;

				// No bus access so no cycle is consumed here
				return true;
			}
			return false;
		}
	};

	// Policy: Implied Addressing Mode
	// Usage: CLC, PHA, INX, etc.
	struct Mode_Implied {
		static bool step(NesCpu& cpu) {
			// No address to fetch.
			// Return true so the Op runs in the cycle immediately following the fetch.
			return true;
		}
	};

	// Policy: Zero Page (e.g., LDA $nn)
	// Cycles: 3 total (T0: Opcode, T1: Fetch Address, T2: Read/Write)
	struct Mode_ZeroPage {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Fetch the 8-bit address
				cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
				// High byte is always 0x00 in Zero Page
				cpu.effective_addr = (0x00 << 8) | cpu.addr_low;

				// No page crossing possible, so we are ready for the Op next cycle
				cpu.cycle_state = 2;
				return false;
			case 2:
				return true;
			}
			
			return false;
		}
	};

	// Policy: Zero Page, X (e.g., LDA $nn, X)
	// Cycles: 4 total (T0: Opcode, T1: Fetch Addr, T2: Add X/Dummy Read, T3: Read/Write)
	struct Mode_ZeroPageX {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
				case 1: // T1: Fetch the base 8-bit address
					cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
					cpu.cycle_state = 2;
					return false;

				case 2: // T2: Add X and perform a Dummy Read
					// Note: The 6502 hardware does a read here at the unindexed address
					cpu.ReadByte((0x00 << 8) | cpu.addr_low);

					// Add X and FORCE wrap within page zero (using & 0xFF)
					cpu.effective_addr = (0x00 << 8) | ((cpu.addr_low + cpu.m_x) & 0xFF);

					// Ready for the Op next cycle
					cpu.cycle_state = 3;
					return false;

				case 3:
					return true;
			}
			return false;
		}
	};

	// Policy: Zero Page, Y (e.g., LDA $nn, Y)
	// Cycles: 4 total (T0: Opcode, T1: Fetch Addr, T2: Add Y/Dummy Read, T3: Read/Write)
	struct Mode_ZeroPageY {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Fetch the base 8-bit address
				cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Add X and perform a Dummy Read
				// Note: The 6502 hardware does a read here at the unindexed address
				cpu.ReadByte((0x00 << 8) | cpu.addr_low);

				// Add X and FORCE wrap within page zero (using & 0xFF)
				cpu.effective_addr = (0x00 << 8) | ((cpu.addr_low + cpu.m_y) & 0xFF);

				// Ready for the Op next cycle
				cpu.cycle_state = 3;
				return false;

			case 3:
				return true;
			}
			return false;
		}
	};

	// Policy: Absolute Addressing Mode ($aaaa)
	// Usage: LDA $1234, STA $4000, etc.
	struct Mode_Absolute {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Fetch Low Byte of Address
				cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Fetch High Byte of Address
				cpu.addr_high = cpu.ReadByte(cpu.m_pc++);

				// Combine into the 16-bit effective address
				cpu.effective_addr = (cpu.addr_high << 8) | cpu.addr_low;

				// In Absolute mode, the address is now fully formed.
				// There is no addition, so no T3 penalty/dummy read is needed.
				cpu.cycle_state = 3;
				return false;
			case 3:
				return true;
			}
			return false;
		}
	};

	// Policy: Absolute X Addressing Mode
	// Template param 'AlwaysPenalty' is true for STA, INC, DEC, ASL, etc.
	template <bool always_penalty>
	struct Mode_AbsoluteX {
		// Returns true when the addressing sequence is totally finished
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // Fetch Low
				cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // Fetch High & Calc
			{
				cpu.addr_high = cpu.ReadByte(cpu.m_pc++);

				// Calculate effective address (wrapping low byte)
				cpu.effective_addr = (cpu.addr_high << 8) | ((cpu.addr_low + cpu.m_x) & 0xFF);

				// Check page crossing
				cpu.page_crossed = ((uint16_t)cpu.addr_low + cpu.m_x) > 0xFF;

				cpu.cycle_state = 3;
				return false;
			}

			case 3: // Potential Dummy Read / Fix Up
				// If it's a WRITE or we crossed a page, we generally strictly need the fixup
				// For READs: if page crossed, we read garbage (dummy), then fix.
				// For READs: if NO cross, we are actually DONE with addressing.
				
				if (always_penalty || cpu.page_crossed) {
					// Do the dummy read (hardware does this)
					cpu.ReadByte(cpu.effective_addr);
					if (cpu.page_crossed) {
						// Fix the high byte for the next cycle
						cpu.effective_addr = ((cpu.addr_high + 1) << 8) | ((cpu.addr_low + cpu.m_x) & 0xFF);
					}
					cpu.cycle_state = 4;
					return false;
				}
				// We are done.
				return true;
			case 4:
				// Address is now fixed. Ready to execute.
				return true;
			}
			return false;
		}
	};

	// Policy: Absolute Y Addressing Mode
	// Template param 'AlwaysPenalty' is true for STA, INC, DEC, ASL, etc.
	template <bool always_penalty>
	struct Mode_AbsoluteY {
		// Returns true when the addressing sequence is totally finished
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // Fetch Low
				cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // Fetch High & Calc
			{
				cpu.addr_high = cpu.ReadByte(cpu.m_pc++);

				// Calculate effective address (wrapping low byte)
				cpu.effective_addr = (cpu.addr_high << 8) | ((cpu.addr_low + cpu.m_y) & 0xFF);

				// Check page crossing
				cpu.page_crossed = ((uint16_t)cpu.addr_low + cpu.m_y) > 0xFF;

				cpu.cycle_state = 3;
				return false;
			}

			case 3: // Potential Dummy Read / Fix Up
				// If it's a WRITE or we crossed a page, we generally strictly need the fixup
				// For READs: if page crossed, we read garbage (dummy), then fix.
				// For READs: if NO cross, we are actually DONE with addressing.

				if (always_penalty || cpu.page_crossed) {
					// Do the dummy read (hardware does this)
					cpu.ReadByte(cpu.effective_addr);
					if (cpu.page_crossed) {
						// Fix the high byte for the next cycle
						cpu.effective_addr = ((cpu.addr_high + 1) << 8) | ((cpu.addr_low + cpu.m_y) & 0xFF);
					}
					cpu.cycle_state = 4;
					return false;
				}
				// We are done.
				return true;
			case 4:
				// Address is now fixed. Ready to execute.
				return true;
			}
			return false;
		}
	};

	// Policy: Indexed Indirect X Addressing Mode ($nn, X)
	// Usage: LDA ($nn, X)
	// This mode is always a fixed length (6 cycles total).
	struct Mode_IndirectX {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
				case 1: // Fetch Zero Page Base Address
					// Read the immediate byte (base address in ZP)
					cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
					cpu.cycle_state = 2;
					return false;

				case 2: // T2: Addition / Dummy Read
					// Hardware adds X to the base address. 
					// It performs a dummy read at the base address.
					cpu.ReadByte(cpu.addr_low);

					// Calculate the actual pointer location (wrapped in ZP)
					cpu.addr_low = (cpu.addr_low + cpu.m_x) & 0xFF;
					cpu.cycle_state = 3;
					return false;

				case 3: // T3: Fetch Effective Address Low
					// Read the low byte of the final address from ZP
					cpu.effective_addr = cpu.ReadByte(cpu.addr_low);
					cpu.cycle_state = 4;
					return false;

				case 4: // T4: Fetch Effective Address High
					// Read the high byte of the final address from ZP+1
					// Must use & 0xFF to ensure ZP wrap-around (FF -> 00)
					{
						uint8_t high = cpu.ReadByte((cpu.addr_low + 1) & 0xFF);
						cpu.effective_addr |= (high << 8);
					}

					// The address is now fully formed.
					// In the 6502 architecture, this mode always completes the 
					// addressing phase here. The Op runs in T5.
					cpu.cycle_state = 5;
					return false;
				case 5:
					return true;
			}
			return false;
		}
	};

	// Policy: Indirect Indexed Y Addressing Mode ($nn), Y
	// Usage: LDA ($nn), Y etc.
	template <bool always_penalty>
	struct Mode_IndirectY {
		// Returns true when the addressing sequence is totally finished
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // Fetch Pointer Address (from Zero Page)
				// We temporarily use effective_addr to hold the ZP pointer address
				cpu.effective_addr = cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // Fetch Effective Address Low Byte
				// Read the low byte of the pointer from ZP
				cpu.addr_low = cpu.ReadByte(cpu.effective_addr);
				cpu.cycle_state = 3;
				return false;

			case 3: // Fetch Effective Address High Byte & Add Y
			{
				// Read the high byte of the pointer from ZP+1 (WITH ZP WRAP!)
				cpu.addr_high = cpu.ReadByte((cpu.effective_addr + 1) & 0xFF);

				// Now we calculate the "tentative" effective address (without carry fix)
				// effective_addr = (Pointer_High << 8) | ((Pointer_Low + Y) & 0xFF)
				cpu.effective_addr = (cpu.addr_high << 8) | ((cpu.addr_low + cpu.m_y) & 0xFF);

				// Check if adding Y crossed a page boundary
				cpu.page_crossed = ((uint16_t)cpu.addr_low + cpu.m_y) > 0xFF;

				cpu.cycle_state = 4;
				return false;
			}

			case 4: // Potential Dummy Read / Fix Up

				if (always_penalty || cpu.page_crossed) {
					// Dummy Read from the "wrong" page (bits 0-7 correct, bits 8-15 wrong)
					cpu.ReadByte(cpu.effective_addr);

					// Fix the high byte for the next cycle
					// Address = ((Pointer_High + 1) << 8) | (Pointer_Low + Y)
					if (cpu.page_crossed) {
						cpu.effective_addr = ((cpu.addr_high + 1) << 8) | ((cpu.addr_low + cpu.m_y) & 0xFF);
					}

					cpu.cycle_state = 5;
					return false;
				}

				// Optimization: If Read & No Cross, we are done.
				// Op will run immediately in this same tick (T4).
				return true;

			case 5:
				// Address is fixed. Ready to execute (T5).
				return true;
			}
			return false;
		}
	};

	// Policy: Read-Modify-Write Operation
	// This policy is used for instructions like INC, DEC, ASL, LSR, etc.
	// RMW instructions benefit from a common state machine since they share the same cycle structure.
	template <typename LogicOp>
	struct Op_RMW {
		static constexpr bool is_write = true;

		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 0: // Read
				cpu._operand = cpu.ReadByte(cpu.effective_addr);
				cpu.cycle_state = 1;
				return false;

			case 1: // Dummy Write + Execute Logic
				cpu.WriteByte(cpu.effective_addr, cpu._operand);

				// Execute the specific math (LSR, INC, etc.)
				LogicOp::execute(cpu, cpu._operand);

				cpu.cycle_state = 2;
				return false;

			case 2: // Write Result
				cpu.WriteByte(cpu.effective_addr, cpu._operand);
				return true;
			}
			return false;
		}
	};

	struct Op_ADC {
		static constexpr bool is_rmw = false;

		static bool step(NesCpu& cpu) {
			return step(cpu, cpu.ReadByte(cpu.effective_addr));
		}
		
		static bool step(NesCpu& cpu, uint8_t operand) {
			uint8_t a_old = cpu.m_a;

			uint16_t result = cpu.m_a + operand + (cpu.m_p & FLAG_CARRY ? 1 : 0);
			cpu.m_a = result & 0xFF;  // Update accumulator with low byte
			// Set/clear carry flag
			if (result > 0xFF) {
				cpu.m_p |= FLAG_CARRY;   // Set carry
			}
			else {
				cpu.m_p &= ~FLAG_CARRY;  // Clear carry
			}
			// Set/clear overflow flag
			// Overflow occurs when:
			// - Adding two positive numbers results in a negative number, OR
			// - Adding two negative numbers results in a positive number
			// This can be detected by checking if the sign bits of both operands
			// are the same, but different from the result's sign bit
			if (((a_old ^ cpu.m_a) & (operand ^ cpu.m_a) & 0x80) != 0) {
				cpu.m_p |= FLAG_OVERFLOW;   // Set overflow
			}
			else {
				cpu.m_p &= ~FLAG_OVERFLOW;  // Clear overflow
			}
			// Set/clear zero flag
			if (cpu.m_a == 0) {
				cpu.m_p |= FLAG_ZERO;
			}
			else {
				cpu.m_p &= ~FLAG_ZERO;
			}

			// Set/clear negative flag (bit 7 of result)
			if (cpu.m_a & 0x80) {
				cpu.m_p |= FLAG_NEGATIVE;
			}
			else {
				cpu.m_p &= ~FLAG_NEGATIVE;
			}
			return true;
		}
	};

	struct Op_AND {
		static bool step(NesCpu& cpu) {
			// AND operation with the accumulator
			cpu.m_a &= cpu.ReadByte(cpu.effective_addr);
			cpu.update_ZN_flags(cpu.m_a);
			return true;
		}
		static constexpr bool is_rmw = false; // Trait used by the Addressing Mode
	};

	struct Op_ASL {
		static constexpr bool is_rmw = true;

		// This version is called for ASL A (Accumulator Mode)
		static bool step_acc(NesCpu& cpu) {
			Logic_ASL::execute(cpu, cpu.m_a);
			return true; // Complete in 2 cycles total
		}
	};

	struct Logic_ASL {
		static void execute(NesCpu& cpu, uint8_t& val) {
			if (val & 0x80) cpu.SetFlag(FLAG_CARRY);
			else cpu.ClearFlag(FLAG_CARRY);

			val <<= 1;
			cpu.update_ZN_flags(val);
		}
	};

	struct Op_BCC {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
			{
				// T1: Fetch the relative offset (signed 8-bit)
				// This ALWAYS happens, regardless of whether we take the branch.
				int8_t offset = (int8_t)cpu.ReadByte(cpu.m_pc++);

				// Check the condition: Branch if Carry is CLEAR (0)
				bool condition_met = !cpu.GetFlag(FLAG_CARRY);

				if (!condition_met) {
					// Condition failed: 2 cycles total. We are done right now.
					return true;
				}

				// If we reach here, the condition is met. 
				// We need at least one more cycle to calculate and apply the branch.
				cpu.offset = offset; // Store offset for next cycle
				cpu.cycle_state = 2;
				return false;
			}
			case 2: // T2: Apply the low byte and check for page cross
			{
				uint16_t old_pc = cpu.m_pc;
				// Add the signed offset to the PC
				cpu.m_pc += cpu.offset;

				// Page crossing occurs if the High Byte of the PC changes
				bool page_crossed = (old_pc & 0xFF00) != (cpu.m_pc & 0xFF00);

				if (page_crossed) {
					// 6502 hardware performs a dummy read here
					cpu.ReadByte((old_pc & 0xFF00) | (cpu.m_pc & 0x00FF));
					cpu.cycle_state = 3;
					return false; // Need one more cycle to fix high byte
				}

				// No page cross: 3 cycles total. Done.
				return true;
			}

			case 3: // T3: Final cycle for page-crossing branch
				// Hardware performs a read at the new, fixed PC
				cpu.ReadByte(cpu.m_pc);
				return true;
			}
			return false;
		}
	};

	struct Op_BCS {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
			{
				// T1: Fetch the relative offset (signed 8-bit)
				// This ALWAYS happens, regardless of whether we take the branch.
				int8_t offset = (int8_t)cpu.ReadByte(cpu.m_pc++);

				// Check the condition: Branch if Carry is CLEAR (0)
				bool condition_met = cpu.GetFlag(FLAG_CARRY);

				if (!condition_met) {
					// Condition failed: 2 cycles total. We are done right now.
					return true;
				}

				// If we reach here, the condition is met. 
				// We need at least one more cycle to calculate and apply the branch.
				cpu.offset = offset; // Store offset for next cycle
				cpu.cycle_state = 2;
				return false;
			}
			case 2: // T2: Apply the low byte and check for page cross
			{
				uint16_t old_pc = cpu.m_pc;
				// Add the signed offset to the PC
				cpu.m_pc += cpu.offset;

				// Page crossing occurs if the High Byte of the PC changes
				bool page_crossed = (old_pc & 0xFF00) != (cpu.m_pc & 0xFF00);

				if (page_crossed) {
					// 6502 hardware performs a dummy read here
					cpu.ReadByte((old_pc & 0xFF00) | (cpu.m_pc & 0x00FF));
					cpu.cycle_state = 3;
					return false; // Need one more cycle to fix high byte
				}

				// No page cross: 3 cycles total. Done.
				return true;
			}

			case 3: // T3: Final cycle for page-crossing branch
				// Hardware performs a read at the new, fixed PC
				cpu.ReadByte(cpu.m_pc);
				return true;
			}
			return false;
		}
	};

	struct Op_BEQ {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
			{
				// T1: Fetch the relative offset (signed 8-bit)
				// This ALWAYS happens, regardless of whether we take the branch.
				int8_t offset = (int8_t)cpu.ReadByte(cpu.m_pc++);

				// Check the condition: Branch if Carry is CLEAR (0)
				bool condition_met = cpu.GetFlag(FLAG_ZERO);

				if (!condition_met) {
					// Condition failed: 2 cycles total. We are done right now.
					return true;
				}

				// If we reach here, the condition is met. 
				// We need at least one more cycle to calculate and apply the branch.
				cpu.offset = offset; // Store offset for next cycle
				cpu.cycle_state = 2;
				return false;
			}
			case 2: // T2: Apply the low byte and check for page cross
			{
				uint16_t old_pc = cpu.m_pc;
				// Add the signed offset to the PC
				cpu.m_pc += cpu.offset;

				// Page crossing occurs if the High Byte of the PC changes
				bool page_crossed = (old_pc & 0xFF00) != (cpu.m_pc & 0xFF00);

				if (page_crossed) {
					// 6502 hardware performs a dummy read here
					cpu.ReadByte((old_pc & 0xFF00) | (cpu.m_pc & 0x00FF));
					cpu.cycle_state = 3;
					return false; // Need one more cycle to fix high byte
				}

				// No page cross: 3 cycles total. Done.
				return true;
			}

			case 3: // T3: Final cycle for page-crossing branch
				// Hardware performs a read at the new, fixed PC
				cpu.ReadByte(cpu.m_pc);
				return true;
			}
			return false;
		}
	};

	struct Op_BIT {
		// Standard read-only operation
		static constexpr bool is_write = false;

		static bool step(NesCpu& cpu) {
			// Addressing mode has already set cpu.effective_addr
			uint8_t mem_val = cpu.ReadByte(cpu.effective_addr);

			// 1. Zero Flag: Result of (A AND M)
			if ((cpu.m_a & mem_val) == 0) cpu.SetFlag(FLAG_ZERO);
			else cpu.ClearFlag(FLAG_ZERO);

			// 2. Negative Flag: Directly from Memory Bit 7
			if (mem_val & 0x80) cpu.SetFlag(FLAG_NEGATIVE);
			else cpu.ClearFlag(FLAG_NEGATIVE);

			// 3. Overflow Flag: Directly from Memory Bit 6
			if (mem_val & 0x40) cpu.SetFlag(FLAG_OVERFLOW);
			else cpu.ClearFlag(FLAG_OVERFLOW);

			return true; // Complete
		}
	};

	struct Op_BMI {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
			{
				// T1: Fetch the relative offset (signed 8-bit)
				// This ALWAYS happens, regardless of whether we take the branch.
				int8_t offset = (int8_t)cpu.ReadByte(cpu.m_pc++);

				// Check the condition: Branch if Negative
				bool condition_met = cpu.GetFlag(FLAG_NEGATIVE);

				if (!condition_met) {
					// Condition failed: 2 cycles total. We are done right now.
					return true;
				}

				// If we reach here, the condition is met. 
				// We need at least one more cycle to calculate and apply the branch.
				cpu.offset = offset; // Store offset for next cycle
				cpu.cycle_state = 2;
				return false;
			}
			case 2: // T2: Apply the low byte and check for page cross
			{
				uint16_t old_pc = cpu.m_pc;
				// Add the signed offset to the PC
				cpu.m_pc += cpu.offset;

				// Page crossing occurs if the High Byte of the PC changes
				bool page_crossed = (old_pc & 0xFF00) != (cpu.m_pc & 0xFF00);

				if (page_crossed) {
					// 6502 hardware performs a dummy read here
					cpu.ReadByte((old_pc & 0xFF00) | (cpu.m_pc & 0x00FF));
					cpu.cycle_state = 3;
					return false; // Need one more cycle to fix high byte
				}

				// No page cross: 3 cycles total. Done.
				return true;
			}

			case 3: // T3: Final cycle for page-crossing branch
				// Hardware performs a read at the new, fixed PC
				cpu.ReadByte(cpu.m_pc);
				return true;
			}
			return false;
		}
	};

	struct Op_BNE {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
			{
				// T1: Fetch the relative offset (signed 8-bit)
				// This ALWAYS happens, regardless of whether we take the branch.
				int8_t offset = (int8_t)cpu.ReadByte(cpu.m_pc++);

				// Check the condition: Branch if Zero flag is not set
				bool condition_met = !cpu.GetFlag(FLAG_ZERO);

				if (!condition_met) {
					// Condition failed: 2 cycles total. We are done right now.
					return true;
				}

				// If we reach here, the condition is met. 
				// We need at least one more cycle to calculate and apply the branch.
				cpu.offset = offset; // Store offset for next cycle
				cpu.cycle_state = 2;
				return false;
			}
			case 2: // T2: Apply the low byte and check for page cross
			{
				uint16_t old_pc = cpu.m_pc;
				// Add the signed offset to the PC
				cpu.m_pc += cpu.offset;

				// Page crossing occurs if the High Byte of the PC changes
				bool page_crossed = (old_pc & 0xFF00) != (cpu.m_pc & 0xFF00);

				if (page_crossed) {
					// 6502 hardware performs a dummy read here
					cpu.ReadByte((old_pc & 0xFF00) | (cpu.m_pc & 0x00FF));
					cpu.cycle_state = 3;
					return false; // Need one more cycle to fix high byte
				}

				// No page cross: 3 cycles total. Done.
				return true;
			}

			case 3: // T3: Final cycle for page-crossing branch
				// Hardware performs a read at the new, fixed PC
				cpu.ReadByte(cpu.m_pc);
				return true;
			}
			return false;
		}
	};

	struct Op_BPL {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
			{
				// T1: Fetch the relative offset (signed 8-bit)
				// This ALWAYS happens, regardless of whether we take the branch.
				int8_t offset = (int8_t)cpu.ReadByte(cpu.m_pc++);

				// Check the condition: Branch if Carry is CLEAR (0)
				bool condition_met = !cpu.GetFlag(FLAG_NEGATIVE);

				if (!condition_met) {
					// Condition failed: 2 cycles total. We are done right now.
					return true;
				}

				// If we reach here, the condition is met. 
				// We need at least one more cycle to calculate and apply the branch.
				cpu.offset = offset; // Store offset for next cycle
				cpu.cycle_state = 2;
				return false;
			}
			case 2: // T2: Apply the low byte and check for page cross
			{
				uint16_t old_pc = cpu.m_pc;
				// Add the signed offset to the PC
				cpu.m_pc += cpu.offset;

				// Page crossing occurs if the High Byte of the PC changes
				bool page_crossed = (old_pc & 0xFF00) != (cpu.m_pc & 0xFF00);

				if (page_crossed) {
					// 6502 hardware performs a dummy read here
					cpu.ReadByte((old_pc & 0xFF00) | (cpu.m_pc & 0x00FF));
					cpu.cycle_state = 3;
					return false; // Need one more cycle to fix high byte
				}

				// No page cross: 3 cycles total. Done.
				return true;
			}

			case 3: // T3: Final cycle for page-crossing branch
				// Hardware performs a read at the new, fixed PC
				cpu.ReadByte(cpu.m_pc);
				return true;
			}
			return false;
		}
	};

	struct Op_BRK {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Dummy Fetch
				// The 6502 fetches the byte after BRK and ignores it
				cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Push PC High Byte
				cpu.WriteByte(0x0100 + cpu.m_sp--, (cpu.m_pc >> 8) & 0xFF);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Push PC Low Byte
				cpu.WriteByte(0x0100 + cpu.m_sp--, cpu.m_pc & 0xFF);
				cpu.cycle_state = 4;
				return false;

			case 4: // T4: Push Status Register (with B flag set)
			{
				// When pushed via BRK, Bit 4 (B flag) and Bit 5 (Unused) are set to 1
				uint8_t status_to_push = cpu.m_p | 0x30;
				cpu.WriteByte(0x0100 + cpu.m_sp--, status_to_push);
				cpu.cycle_state = 5;
				return false;
			}

			case 5: { // T5: Fetch Vector Low
				// if NMI is asserted during the first four ticks of a BRK instruction,
				// the BRK instruction will execute normally at first (PC increments will occur and
				// the status word will be pushed with the B flag set), but execution will branch to
				// the NMI vector instead of the IRQ/BRK vector
				if (cpu.nmi_need) {
					cpu.addr_low = cpu.ReadByte(0xFFFA);
				}
				else {
					cpu.addr_low = cpu.ReadByte(0xFFFE);
				}
				// The interrupt flag is set internally to prevent nested interrupts
				cpu.SetFlag(FLAG_INTERRUPT);
				cpu.cycle_state = 6;
				return false;
			}
			case 6: // T6: Fetch Vector High and Jump
				if (cpu.nmi_need) {
					cpu.addr_high = cpu.ReadByte(0xFFFB);
					cpu.nmi_need = false; // Clear NMI request
				}
				else {
					cpu.addr_high = cpu.ReadByte(0xFFFF);
				}
				cpu.m_pc = (cpu.addr_high << 8) | cpu.addr_low;
				cpu.nmi_previous_need = false;
				return true; // Complete
			}
			return false;
		}
	};

	struct Op_BVC {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
			{
				// T1: Fetch the relative offset (signed 8-bit)
				// This ALWAYS happens, regardless of whether we take the branch.
				int8_t offset = (int8_t)cpu.ReadByte(cpu.m_pc++);

				// Check the condition: Branch if Overflow flag is not set
				bool condition_met = !cpu.GetFlag(FLAG_OVERFLOW);

				if (!condition_met) {
					// Condition failed: 2 cycles total. We are done right now.
					return true;
				}

				// If we reach here, the condition is met. 
				// We need at least one more cycle to calculate and apply the branch.
				cpu.offset = offset; // Store offset for next cycle
				cpu.cycle_state = 2;
				return false;
			}
			case 2: // T2: Apply the low byte and check for page cross
			{
				uint16_t old_pc = cpu.m_pc;
				// Add the signed offset to the PC
				cpu.m_pc += cpu.offset;

				// Page crossing occurs if the High Byte of the PC changes
				bool page_crossed = (old_pc & 0xFF00) != (cpu.m_pc & 0xFF00);

				if (page_crossed) {
					// 6502 hardware performs a dummy read here
					cpu.ReadByte((old_pc & 0xFF00) | (cpu.m_pc & 0x00FF));
					cpu.cycle_state = 3;
					return false; // Need one more cycle to fix high byte
				}

				// No page cross: 3 cycles total. Done.
				return true;
			}

			case 3: // T3: Final cycle for page-crossing branch
				// Hardware performs a read at the new, fixed PC
				cpu.ReadByte(cpu.m_pc);
				return true;
			}
			return false;
		}
	};

	struct Op_BVS {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1:
			{
				// T1: Fetch the relative offset (signed 8-bit)
				// This ALWAYS happens, regardless of whether we take the branch.
				int8_t offset = (int8_t)cpu.ReadByte(cpu.m_pc++);

				// Check the condition: Branch if Overflow flag is not set
				bool condition_met = cpu.GetFlag(FLAG_OVERFLOW);

				if (!condition_met) {
					// Condition failed: 2 cycles total. We are done right now.
					return true;
				}

				// If we reach here, the condition is met. 
				// We need at least one more cycle to calculate and apply the branch.
				cpu.offset = offset; // Store offset for next cycle
				cpu.cycle_state = 2;
				return false;
			}
			case 2: // T2: Apply the low byte and check for page cross
			{
				uint16_t old_pc = cpu.m_pc;
				// Add the signed offset to the PC
				cpu.m_pc += cpu.offset;

				// Page crossing occurs if the High Byte of the PC changes
				bool page_crossed = (old_pc & 0xFF00) != (cpu.m_pc & 0xFF00);

				if (page_crossed) {
					// 6502 hardware performs a dummy read here
					cpu.ReadByte((old_pc & 0xFF00) | (cpu.m_pc & 0x00FF));
					cpu.cycle_state = 3;
					return false; // Need one more cycle to fix high byte
				}

				// No page cross: 3 cycles total. Done.
				return true;
			}

			case 3: // T3: Final cycle for page-crossing branch
				// Hardware performs a read at the new, fixed PC
				cpu.ReadByte(cpu.m_pc);
				return true;
			}
			return false;
		}
	};

	struct Op_CLC {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Clear the Carry Flag
			cpu.ClearFlag(FLAG_CARRY);
			// Instruction complete
			return true;
		}
	};

	struct Op_CLD {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			cpu.ClearFlag(FLAG_DECIMAL);
			return true;
		}
	};

	struct Op_CLI {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			cpu.ClearFlag(FLAG_INTERRUPT);
			return true;
		}
	};

	struct Op_CLV {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			cpu.ClearFlag(FLAG_OVERFLOW);
			return true;
		}
	};

	struct Op_CMP {
		// Defines that this is a Read operation (allows page-cross optimization)
		static constexpr bool is_rmw = false;

		static bool step(NesCpu& cpu) {
			// This runs once the Addressing Mode has set cpu.effective_addr
			uint8_t val = cpu.ReadByte(cpu.effective_addr);

			uint16_t result = (uint16_t)cpu.m_a - (uint16_t)val;

			// Carry Flag: Set if A >= val (No borrow occurred)
			if (cpu.m_a >= val) cpu.SetFlag(FLAG_CARRY);
			else cpu.ClearFlag(FLAG_CARRY);

			// Zero and Negative flags are based on the 8-bit result
			cpu.update_ZN_flags((uint8_t)(result & 0xFF));

			return true; // Instruction complete
		}
	};

	struct Op_CPX {
		static constexpr bool is_rmw = false;
		static bool step(NesCpu& cpu) {
			uint8_t val = cpu.ReadByte(cpu.effective_addr);
			if (cpu.m_x >= val) cpu.SetFlag(FLAG_CARRY);
			else cpu.ClearFlag(FLAG_CARRY);
			cpu.update_ZN_flags((uint8_t)(cpu.m_x - val));
			return true;
		}
	};

	struct Op_CPY {
		static constexpr bool is_rmw = false;
		static bool step(NesCpu& cpu) {
			uint8_t val = cpu.ReadByte(cpu.effective_addr);
			if (cpu.m_y >= val) cpu.SetFlag(FLAG_CARRY);
			else cpu.ClearFlag(FLAG_CARRY);
			cpu.update_ZN_flags((uint8_t)(cpu.m_y - val));
			return true;
		}
	};

	struct Logic_DEC {
		static void execute(NesCpu& cpu, uint8_t& val) {
			val--;
			cpu.update_ZN_flags(val);
		}
	};

	struct Op_DEX {
		static bool step(NesCpu& cpu) {
			// T1: Internal Operation
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			cpu.m_x--;
			// Update Zero and Negative flags based on new X value
			cpu.update_ZN_flags(cpu.m_x);

			// Instruction complete (2 cycles total: T0, T1)
			return true;
		}
	};

	struct Op_DEY {
		static bool step(NesCpu& cpu) {
			// T1: Internal Operation
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			cpu.m_y--;
			// Update Zero and Negative flags based on new X value
			cpu.update_ZN_flags(cpu.m_y);

			// Instruction complete (2 cycles total: T0, T1)
			return true;
		}
	};

	struct Op_EOR {
		// Allows page-cross optimization (Read operation)
		static constexpr bool is_write = false;

		static bool step(NesCpu& cpu) {
			// Fetch the value from the address prepared by the Mode
			uint8_t val = cpu.ReadByte(cpu.effective_addr);

			// Perform the Exclusive OR
			cpu.m_a ^= val;

			// Update Zero and Negative flags based on the Accumulator
			cpu.update_ZN_flags(cpu.m_a);

			return true; // Complete
		}
	};

	struct Op_HardwareInterrupt_NMI {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Another Dummy Read (The 6502 is quirky like this)
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Push PC High
				cpu.WriteByte(0x0100 + cpu.m_sp--, (cpu.m_pc >> 8) & 0xFF);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Push PC Low
				cpu.WriteByte(0x0100 + cpu.m_sp--, cpu.m_pc & 0xFF);
				cpu.cycle_state = 4;
				return false;

			case 4: // T4: Push Status
			{
				uint8_t p = (cpu.m_p & 0xEF) | 0x20; // Clear B flag, set Unused flag
				cpu.WriteByte(0x0100 + cpu.m_sp--, p);
				cpu.cycle_state = 5;
				return false;
			}
			case 5: // T5: Fetch Vector Low
				cpu.addr_low = cpu.ReadByte(0xFFFA);
				cpu.SetFlag(FLAG_INTERRUPT);
				cpu.cycle_state = 6;
				return false;

			case 6: // T6: Fetch Vector High
				cpu.addr_high = cpu.ReadByte(0xFFFB);
				cpu.m_pc = (cpu.addr_high << 8) | cpu.addr_low;
				cpu.cycle_state = 0; // Reset
				return true;
			}
			return false;
		}
	};

	struct Op_HardwareInterrupt_IRQ {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Another Dummy Read (The 6502 is quirky like this)
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Push PC High
				cpu.WriteByte(0x0100 + cpu.m_sp--, (cpu.m_pc >> 8) & 0xFF);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Push PC Low
				cpu.WriteByte(0x0100 + cpu.m_sp--, cpu.m_pc & 0xFF);
				cpu.cycle_state = 4;
				return false;

			case 4: // T4: Push Status
			{
				// Disable the Break flag, set the Unused flag.
				uint8_t p = (cpu.m_p & 0xEF) | 0x20;
				cpu.WriteByte(0x0100 + cpu.m_sp--, p);
				cpu.cycle_state = 5;
				return false;
			}
			case 5: // T5: Fetch Vector Low
				if (cpu.nmi_need) {
					cpu.addr_low = cpu.ReadByte(0xFFFA);
				}
				else {
					cpu.addr_low = cpu.ReadByte(0xFFFE);
				}
				cpu.SetFlag(FLAG_INTERRUPT);
				cpu.cycle_state = 6;
				return false;

			case 6: // T6: Fetch Vector High
				if (cpu.nmi_need) {
					cpu.addr_high = cpu.ReadByte(0xFFFB);
					cpu.nmi_need = false; // Clear NMI request
				}
				else {
					cpu.addr_high = cpu.ReadByte(0xFFFF);
				}
				cpu.m_pc = (cpu.addr_high << 8) | cpu.addr_low;
				cpu.cycle_state = 0; // Reset
				return true;
			}
			return false;
		}
	};

	struct Op_HardwareReset {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Initial dummy read
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Dummy read (Stack Pointer is typically decremented but not written to)
				cpu.ReadByte(0x0100 + cpu.m_sp--);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Dummy read 
				cpu.ReadByte(0x0100 + cpu.m_sp--);
				cpu.cycle_state = 4;
				return false;

			case 4: // T4: Dummy read
				cpu.ReadByte(0x0100 + cpu.m_sp--);
				cpu.cycle_state = 5;
				return false;

			case 5: // T5: Fetch Reset Vector Low
				cpu.addr_low = cpu.ReadByte(0xFFFC);
				// The Interrupt flag is set to prevent IRQs during bootup
				cpu.SetFlag(FLAG_INTERRUPT);
				cpu.cycle_state = 6;
				return false;

			case 6: // T6: Fetch Reset Vector High
				cpu.addr_high = cpu.ReadByte(0xFFFD);
				cpu.m_pc = (cpu.addr_high << 8) | cpu.addr_low;

				// Reset internal interrupt latches to prevent stale signals
				cpu.nmi_need = false;
				cpu.nmi_previous_need = false;

				cpu.cycle_state = 0;
				cpu.reset_line = false;

				cpu.irq_line = false;
				cpu.prev_run_irq = false;
				cpu.run_irq = false;
				return true; // Complete
			}
			return false;
		}
	};

	struct Op_HLT {
		static bool step(NesCpu& cpu) {
			cpu.m_pc -= 1; // Stay on this instruction
			return true;
		}
		static constexpr bool is_rmw = false; // Trait used by the Addressing Mode
	};

	struct Logic_INC {
		static void execute(NesCpu& cpu, uint8_t& val) {
			val++;
			cpu.update_ZN_flags(val);
		}
	};

	struct Op_INX {
		static bool step(NesCpu& cpu) {
			// Cycle T1: Internal Operation
			// Dummy pc fetch
			cpu.ReadByte(cpu.m_pc);
			cpu.m_x++;
			cpu.update_ZN_flags(cpu.m_x);

			return true; // Complete in T1 (Total 2 cycles including T0 fetch)
		}
	};

	struct Op_INY {
		static bool step(NesCpu& cpu) {
			// Dummy pc fetch
			cpu.ReadByte(cpu.m_pc);
			cpu.m_y++;
			cpu.update_ZN_flags(cpu.m_y);
			return true;
		}
	};

	struct Op_JMP_Absolute {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Fetch Low Byte
				cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Fetch High Byte and Jump
				cpu.addr_high = cpu.ReadByte(cpu.m_pc);
				cpu.m_pc = (cpu.addr_high << 8) | cpu.addr_low;

				// Instruction complete in 3 cycles total (T0, T1, T2)
				return true;
			}
			return false;
		}
	};

	struct Op_JMP_Indirect {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Fetch Pointer Low
				cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Fetch Pointer High
				cpu.addr_high = cpu.ReadByte(cpu.m_pc++);
				// effective_addr now holds the location of the target vector
				cpu.effective_addr = (cpu.addr_high << 8) | cpu.addr_low;
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Fetch Target Low
				cpu.m_temp_low = cpu.ReadByte(cpu.effective_addr);
				cpu.cycle_state = 4;
				return false;

			case 4: // T4: Fetch Target High (With Hardware Bug)
			{
				uint16_t target_high_addr;

				// Replicate the 6502 bug: 
				// If the low byte of the pointer is 0xFF, the high byte is fetched
				// from the start of the same page rather than crossing the page.
				if ((cpu.effective_addr & 0xFF) == 0xFF) {
					target_high_addr = cpu.effective_addr & 0xFF00; // Wraps to $xx00
				}
				else {
					target_high_addr = cpu.effective_addr + 1;
				}

				uint8_t target_high = cpu.ReadByte(target_high_addr);
				cpu.m_pc = (target_high << 8) | cpu.m_temp_low;

				return true; // 5 cycles total
			}
			}
			return false;
		}
	};

	struct Op_JSR {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Fetch Low Byte of destination
				cpu.addr_low = cpu.ReadByte(cpu.m_pc++);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Internal Operation (Stack Pointer preparation)
				// Dummy read of stack pointer
				cpu.ReadByte(cpu.m_sp);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Push PC High Byte to Stack
				// Note: We push the current PC, which points to the high byte 
				// of the JSR operand (the return address - 1).
				cpu.WriteByte(0x0100 + cpu.m_sp--, (cpu.m_pc >> 8) & 0xFF);
				cpu.cycle_state = 4;
				return false;

			case 4: // T4: Push PC Low Byte to Stack
				cpu.WriteByte(0x0100 + cpu.m_sp--, cpu.m_pc & 0xFF);
				cpu.cycle_state = 5;
				return false;

			case 5: // T5: Fetch High Byte and Update PC
				cpu.addr_high = cpu.ReadByte(cpu.m_pc);
				// Final destination jump
				cpu.m_pc = (cpu.addr_high << 8) | cpu.addr_low;

				// Instruction is complete.
				return true;
			}
			return false;
		}
	};

	struct Op_LDA {
		static bool step(NesCpu& cpu) {
			cpu.m_a = cpu.ReadByte(cpu.effective_addr);
			cpu.update_ZN_flags(cpu.m_a);
			return true;
		}
		static constexpr bool is_rmw = false; // Trait used by the Addressing Mode
	};

	struct Op_LDX {
		static bool step(NesCpu& cpu) {
			cpu.m_x = cpu.ReadByte(cpu.effective_addr);
			cpu.update_ZN_flags(cpu.m_x);
			return true;
		}
		static constexpr bool is_rmw = false; // Trait used by the Addressing Mode
	};

	struct Op_LDY {
		static bool step(NesCpu& cpu) {
			cpu.m_y = cpu.ReadByte(cpu.effective_addr);
			cpu.update_ZN_flags(cpu.m_y);
			return true;
		}
		static constexpr bool is_rmw = false; // Trait used by the Addressing Mode
	};

	struct Op_LSR_Accumulator {
		static bool step(NesCpu& cpu) {
			Logic_LSR::execute(cpu, cpu.m_a);
			return true;
		}
	};

	struct Logic_LSR {
		static void execute(NesCpu& cpu, uint8_t& val) {
			if (val & 0x01) cpu.SetFlag(FLAG_CARRY);
			else cpu.ClearFlag(FLAG_CARRY);
			val >>= 1;
			cpu.update_ZN_flags(val);
		}
	};

	struct Op_NOP {
		static bool step(NesCpu& cpu) {
			// T1: Internal Operation
			// The 6502 performs a dummy read of the next byte after the opcode 
			// and ignores it. No registers or flags are modified.
			cpu.ReadByte(cpu.m_pc);
			return true; // Complete (2 cycles total)
		}
	};

	struct Op_ORA {
		// Allows page-cross optimization (Read operation)
		static constexpr bool is_rmw = false;

		static bool step(NesCpu& cpu) {
			// Fetch the value from the address prepared by the Mode
			uint8_t val = cpu.ReadByte(cpu.effective_addr);

			// Perform the Exclusive OR
			cpu.m_a |= val;

			// Update Zero and Negative flags based on the Accumulator
			cpu.update_ZN_flags(cpu.m_a);

			return true; // Complete
		}
	};

	struct Op_PHA {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Dummy Read
				// Hardware quirk: reads the next byte (PC) but ignores it
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Write Accumulator to Stack
				// The stack is at Page 1 ($0100 - $01FF)
				cpu.WriteByte(0x0100 + cpu.m_sp--, cpu.m_a);

				// Instruction complete
				return true;
			}
			return false;
		}
	};

	struct Op_PHP {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Dummy Read
				// Hardware quirk: reads the next byte (PC) but ignores it
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Write Accumulator to Stack
				// The stack is at Page 1 ($0100 - $01FF)
				cpu.WriteByte(0x0100 + cpu.m_sp--, cpu.m_p | FLAG_BREAK | 0x20);

				// Instruction complete
				return true;
			}
			return false;
		}
	};

	struct Op_PLA {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Dummy Read
				// Hardware reads the opcode address again (internal sync)
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Internal Operation
				// The CPU uses this cycle to increment the Stack Pointer (SP++)
				// No bus activity is needed, but we do a dummy read to stay in sync.
				cpu.ReadByte(0x0100 + cpu.m_sp);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Pull and Update Flags
				// 1. Increment SP and read from the stack
				cpu.m_a = cpu.ReadByte(0x0100 + ++cpu.m_sp);

				// 2. PLA affects the Zero and Negative flags
				cpu.update_ZN_flags(cpu.m_a);

				// Instruction complete (4 cycles total: T0, T1, T2, T3)
				return true;
			}
			return false;
		}
	};

	struct Op_PLP {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Dummy Read
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Internal Operation (SP++)
				cpu.ReadByte(0x0100 + cpu.m_sp);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Pull Status Register
			{
				uint8_t pulled_p = cpu.ReadByte(0x0100 + ++cpu.m_sp);
				// Ignoring the Break and Unused flags.
				cpu.m_p = (pulled_p & 0b11001111) | (cpu.m_p & ~0b11001111);
				return true; // 4 cycles total
			}
			}
			return false;
		}
	};

	struct Op_ROL_Accumulator {
		static bool step(NesCpu& cpu) {
			Logic_ROL::execute(cpu, cpu.m_a);
			return true;
		}
	};

	struct Logic_ROL {
		static void execute(NesCpu& cpu, uint8_t& val) {
			uint8_t old_carry = cpu.GetFlag(FLAG_CARRY) ? 1 : 0;

			// Set new Carry from old Bit 7
			if (val & 0x80) cpu.SetFlag(FLAG_CARRY);
			else cpu.ClearFlag(FLAG_CARRY);

			// Shift and insert old carry
			val = (val << 1) | old_carry;

			cpu.update_ZN_flags(val);
		}
	};

	struct Op_ROR_Accumulator {
		static bool step(NesCpu& cpu) {
			Logic_ROR::execute(cpu, cpu.m_a);
			return true;
		}
	};

	struct Logic_ROR {
		static void execute(NesCpu& cpu, uint8_t& val) {
			// 1. Capture the OLD Carry bit (0 or 1)
			uint8_t old_carry = cpu.GetFlag(FLAG_CARRY) ? 1 : 0;

			// 2. Set NEW Carry based on Bit 0 of A
			if (val & 0x01) cpu.SetFlag(FLAG_CARRY);
			else cpu.ClearFlag(FLAG_CARRY);

			// 3. Perform the shift and merge in old carry
			val = (val >> 1) | (old_carry ? 0x80 : 0);

			// 4. Update Z and N
			cpu.update_ZN_flags(val);
		}
	};

	struct Op_RTI {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Dummy Read
				// The 6502 performs a dummy read of the opcode byte (or stack)
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Increment Stack Pointer (Preparation)
				// Internal cycle to prepare stack hardware
				cpu.ReadByte(cpu.m_sp);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Pull Status Register (P)
				// Pull the flags back from the stack
				cpu.m_p = cpu.ReadByte(0x0100 + ++cpu.m_sp);
				// Ensure the unused bit (bit 5) and the Break bit (bit 4) are handled 
				// per NES specifics (Bit 5 usually stays 1)
				cpu.m_p = (cpu.m_p & 0xEF) | FLAG_UNUSED;

				cpu.cycle_state = 4;
				return false;

			case 4: // T4: Pull Program Counter Low (PC L)
				cpu.addr_low = cpu.ReadByte(0x0100 + ++cpu.m_sp);
				cpu.cycle_state = 5;
				return false;

			case 5: // T5: Pull Program Counter High (PC H)
				cpu.addr_high = cpu.ReadByte(0x0100 + ++cpu.m_sp);

				// Set the PC to the pulled address
				cpu.m_pc = (cpu.addr_high << 8) | cpu.addr_low;

				// In RTI, the address pulled is the actual return address.
				// No increment is needed.
				return true;
			}
			return false;
		}
	};

	struct Op_RTS {
		static bool step(NesCpu& cpu) {
			switch (cpu.cycle_state) {
			case 1: // T1: Dummy Read
				// The 6502 performs a dummy read of the opcode byte (internal processing)
				cpu.ReadByte(cpu.m_pc);
				cpu.cycle_state = 2;
				return false;

			case 2: // T2: Internal Operation (Stack Pointer increment)
				// Preparing the stack hardware
				// Dummy read
				cpu.ReadByte(cpu.m_sp);
				cpu.cycle_state = 3;
				return false;

			case 3: // T3: Pull Program Counter Low (PC L)
				cpu.addr_low = cpu.ReadByte(0x0100 + ++cpu.m_sp);
				cpu.cycle_state = 4;
				return false;

			case 4: // T4: Pull Program Counter High (PC H)
				cpu.addr_high = cpu.ReadByte(0x0100 + ++cpu.m_sp);
				cpu.m_pc = (cpu.addr_high << 8) | cpu.addr_low;
				cpu.cycle_state = 5;
				return false;

			case 5: // T5: Increment PC (The "Return Address + 1" rule)
				// Hardware quirk: RTS pulls PC, then reads from it, 
				// then increments it to find the real next opcode.
				cpu.ReadByte(cpu.m_pc++);

				// Instruction is now complete.
				return true;
			}
			return false;
		}
	};

	struct Op_SBC {
		static bool step(NesCpu& cpu) {
			// Invert the operand for subtraction
			return Op_ADC::step(cpu, ~cpu.ReadByte(cpu.effective_addr));
		}
		static constexpr bool is_rmw = false; // Trait used by the Addressing Mode
	};

	struct Op_SEC {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Clear the Carry Flag
			cpu.SetFlag(FLAG_CARRY);
			// Instruction complete
			return true;
		}
	};

	struct Op_SED {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Clear the Carry Flag
			cpu.SetFlag(FLAG_DECIMAL);
			// Instruction complete
			return true;
		}
	};

	struct Op_SEI {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Clear the Carry Flag
			cpu.SetFlag(FLAG_INTERRUPT);
			// Instruction complete
			return true;
		}
	};

	struct Op_STA {
		static bool step(NesCpu& cpu) {
			cpu.WriteByte(cpu.effective_addr, cpu.m_a);
			return true;
		}
		static constexpr bool is_rmw = true;
	};

	struct Op_STX {
		static bool step(NesCpu& cpu) {
			cpu.WriteByte(cpu.effective_addr, cpu.m_x);
			return true;
		}
		static constexpr bool is_rmw = true;
	};

	struct Op_STY {
		static bool step(NesCpu& cpu) {
			cpu.WriteByte(cpu.effective_addr, cpu.m_y);
			return true;
		}
		static constexpr bool is_rmw = true;
	};

	struct Op_TAX {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Internal Operation
			// Copy the value
			cpu.m_x = cpu.m_a;

			// Update flags based on the NEW value in X
			cpu.update_ZN_flags(cpu.m_x);

			return true; // Complete (2 cycles total)
		}
	};

	struct Op_TAY {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Internal Operation
			// Copy the value
			cpu.m_y = cpu.m_a;

			// Update flags based on the NEW value in X
			cpu.update_ZN_flags(cpu.m_y);

			return true; // Complete (2 cycles total)
		}
	};

	struct Op_TSX {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Internal Operation
			// Copy the value
			cpu.m_x = cpu.m_sp;

			// Update flags based on the NEW value in X
			cpu.update_ZN_flags(cpu.m_x);

			return true; // Complete (2 cycles total)
		}
	};

	struct Op_TXA {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Internal Operation
			// Copy the value
			cpu.m_a = cpu.m_x;

			// Update flags based on the NEW value in X
			cpu.update_ZN_flags(cpu.m_a);

			return true; // Complete (2 cycles total)
		}
	};

	struct Op_TXS {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Internal Operation
			// Copy the value
			cpu.m_sp = cpu.m_x;

			return true; // Complete (2 cycles total)
		}
	};

	struct Op_TYA {
		static bool step(NesCpu& cpu) {
			// Dummy read
			cpu.ReadByte(cpu.m_pc);
			// T1: Internal Operation
			// Copy the value
			cpu.m_a = cpu.m_y;

			// Update flags based on the NEW value in X
			cpu.update_ZN_flags(cpu.m_a);

			return true; // Complete (2 cycles total)
		}
	};

	template <typename Op>
	static void run_accumulator_instruction(NesCpu& cpu) {
		// Accumulator instructions are always 2 cycles.
		// T0 was the fetch. T1 is the execution.
		if (Op::step_acc(cpu)) {
			cpu.inst_complete = true;
			cpu.cycle_state = 0;
			cpu.addr_complete = false;
		}
	}

	template <typename Op>
	static void run_standalone_instruction(NesCpu& cpu) {
		// Branches don't have a standard Effective Address mode, addressing is done in the branch logic
		if (Op::step(cpu)) {
			cpu.inst_complete = true;
			cpu.cycle_state = 0;
			cpu.addr_complete = false;
		}
	}

	template <typename Mode, typename Op>
	static void run_instruction(NesCpu& cpu) {
		// 1. Run the Addressing Mode logic
		if (!cpu.addr_complete) {
			// Run Addressing Mode logic
			// Returns TRUE when the effective_addr is ready on the bus
			cpu.addr_complete = Mode::step(cpu);
			if (cpu.addr_complete) {
				// Reset cycle state for Op execution
				cpu.cycle_state = 0;
			}
		}
		if (cpu.addr_complete) {
			// See notes way above on why we run the op immediately after addressing completes
			// Run Operation logic
			// Returns TRUE when the instruction is fully complete
			bool finished = Op::step(cpu);
			if (finished) {
				// Reset state for next opcode
				cpu.inst_complete = true;
				cpu.cycle_state = 0;
				cpu.addr_complete = false;
				// Trigger fetch of next opcode here or via main loop
			}
		}
	}

	// Define a function pointer type for our micro-op handlers
	typedef void (*InstructionHandler)(NesCpu&);

	// The Lookup Table
	InstructionHandler opcode_table[259];

	OpenBusMapper& openBus;
	DebuggerContext& dbgCtx;
	SharedContext& sharedCtx;
	PPU& ppu;
	void push(uint8_t value);
	uint8_t pull();

	// Interrupt lines
	bool nmi_line;
	bool nmi_previous;
	bool nmi_previous_need;
	bool nmi_need;

	bool prev_run_irq;
	bool run_irq;
	bool irq_line;

	bool reset_line;

	int checkInterrupts();
	void handleNMI();
	void handleIRQ();

	
	//void _and(uint8_t operand);
	uint64_t m_cycle_count = 0;
	// Program counter
	int m_pc;
	uint8_t m_sp = 0xFD;
	inline void dbg(const wchar_t* fmt, ...);
	inline void dbgNmi(const wchar_t* fmt, ...);

	void buildMap();

	// flags
	uint8_t m_p;

	bool isActive = false;

	inline void SetZero(uint8_t value);
	inline void SetNegative(uint8_t value);
	inline void SetOverflow(bool condition);
	inline void SetCarry(bool condition);
	inline void SetDecimal(bool condition);
	inline void SetInterrupt(bool condition);
	inline void SetBreak(bool condition);
	inline uint16_t ReadIndirectIndexed();
	int nmiCount = 0;

	bool isFrozen = false;
	int count = 0;
};