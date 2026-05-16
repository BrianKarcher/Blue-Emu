#include "NesBus.h"
#include <Windows.h>
#include "NesPpu.h"
#include "RendererLoopy.h"
#include "OpenNesBusMapper.h"
#include "Serializer.h"
#include "DebuggerContext.h"
#include "SharedContext.h"
#include "NesCpu.h"

#include <thread>
#include <chrono>

NesCpu::NesCpu(OpenNesBusMapper& openNesBus, SharedContext& ctx, DebuggerContext& dbg, NesPpu& ppu) : openNesBus(openNesBus), sharedCtx(ctx), dbgCtx(dbg), ppu(ppu) {
	init_cpu();
}

void NesCpu::connectNesBus(NesBus* bus) {
	this->bus = bus;
}

bool NesCpu::ShouldPause() {
	if (dbgCtx.HasBreakpoint(m_pc) && !dbgCtx.is_paused.load(std::memory_order_relaxed)) {
		dbgCtx.is_paused.store(true);
		dbgCtx.LogInstructionFetch(m_pc);
		dbgCtx.lastState.a = m_a;
		dbgCtx.lastState.x = m_x;
		dbgCtx.lastState.y = m_y;
		dbgCtx.lastState.sp = m_sp;
		dbgCtx.lastState.p = m_p;
		dbgCtx.lastState.pc = m_pc; // Pointing to the opcode just executed/fetched
		// Notify the Debugger UI thread
		//PostMessage(dbgCtx.hwndDbg, WM_USER_BREAKPOINT_HIT, 0, 0);
		dbgCtx.hit_breakpoint.store(true);
		ppu.UpdateState();
	}

	if (dbgCtx.is_paused.load(std::memory_order_relaxed)) {
		if (dbgCtx.step_requested.load(std::memory_order_relaxed)) {
			dbgCtx.step_requested.store(false); // Consume the request
			return false; // allow ONE instruction
		}
		if (dbgCtx.continue_requested.load(std::memory_order_relaxed)) {
			dbgCtx.continue_requested.store(false); // Consume the request
			dbgCtx.is_paused.store(false);
			return false; // continue running
		}
		return true;
	}

	return false;
}

/// <summary>
/// As long as the game is not paused, execute one and only one CPU cycle.
/// This may involve fetching a new instruction or executing a micro-op of the current instruction.
/// "Always run once" keeps the CPU, NesApu and NesPpu in sync.
/// Note the difference between instructions and cycles.
/// This function gets called 1.79 million times per second.
/// The state machines are designed to run on a per-cycle basis. On each cycle, a bus access MUST occur.
/// Although some are dummy reads/writes, they must happen to keep the timing correct.
/// </summary>
void NesCpu::cpu_tick() {
	if (!isActive) return;
	if (inst_complete) {
		while (ShouldPause() && sharedCtx.is_running) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		// Priority 1: reset
		if (reset_line) {
			current_opcode = OP_RESET;
		}
		else if (nmi_previous_need) {
			// Hardware puts PC on address bus but ignores the data returned,
			// then prepares the NMI sequence.
			LOG_NMI(L"NMI triggered at cycle %llu\n", m_cycle_count);
			ReadByte(m_pc);
			current_opcode = OP_NMI;
			nmi_previous_need = false;
			nmi_need = false;
		}
		// Priority 2: IRQ
		else if (prev_run_irq) {
			// Dummy read
			ReadByte(m_pc);
			current_opcode = OP_IRQ;
		}
		// Priority 3: Normal Fetch
		else {
			// This is the actual T0 Read.
			dbgCtx.LogInstructionFetch(m_pc);
			dbgCtx.lastState.a = m_a;
			dbgCtx.lastState.x = m_x;
			dbgCtx.lastState.y = m_y;
			dbgCtx.lastState.sp = m_sp;
			dbgCtx.lastState.p = m_p;
			dbgCtx.lastState.pc = m_pc; // Pointing to the opcode just executed/fetched
			current_opcode = ReadByte(m_pc++);
		}
		cycle_state = 1;
		inst_complete = false;
	}
	else {
		// Execute the micro-op for the current instruction
		opcode_table[current_opcode](*this);
	}

	//"This edge detector polls the status of the NMI line during o2 of each CPU cycle (i.e., during the 
	//second half of each cycle) and raises an internal signal if the input goes from being high during 
	//one cycle to being low during the next"
	nmi_previous_need = nmi_need;

	if (!nmi_previous && nmi_line) {
		nmi_need = true;
	}
	nmi_previous = nmi_line;

	// "it's really the status of the interrupt lines at the end of the second-to-last cycle that matters."
	prev_run_irq = run_irq;
	run_irq = irq_line && (GetFlag(FLAG_INTERRUPT) == 0);
	m_cycle_count++;
}

/* We emulate the 6502 only as far as it is compatible with the NES. For example, we do not include Decimal Mode.*/

// Reference https://www.nesdev.org/obelisk-6502-guide/reference.html
int cnt = 0;
int cnt2 = 0;

void NesCpu::Serialize(Serializer& serializer) {
	CPUState cpu;
	cpu.m_a = m_a;
	cpu.m_x = m_x;
	cpu.m_y = m_y;
	cpu.m_pc = m_pc;
	cpu.m_sp = m_sp;
	cpu.m_p = m_p;
	cpu.nmi_line = nmi_line;
	cpu.nmi_previous = nmi_previous;
	cpu.nmi_previous_need = nmi_previous_need;
	cpu.irq_line = irq_line;

	cpu.m_cycle_count = m_cycle_count;
	cpu.nmi_need = nmi_need;
	cpu.current_opcode = current_opcode;

	cpu.addr_low = addr_low;
	cpu.addr_high = cpu.addr_high;
	cpu.m_temp_low = m_temp_low;
	cpu.effective_addr = effective_addr;
	cpu.offset = offset;
	cpu.inst_complete = inst_complete;
	cpu.addr_complete = addr_complete;
	cpu._operand = _operand;
	cpu.cycle_state = cycle_state;  // Tracks the current micro-op (0, 1, 2...)
	cpu.page_crossed = page_crossed;

	cpu.prev_run_irq = prev_run_irq;
	cpu.run_irq = run_irq;
	serializer.Write(cpu);
}

void NesCpu::Deserialize(Serializer& serializer) {
	CPUState cpu;
	serializer.Read(cpu);
	m_a = cpu.m_a;
	m_x = cpu.m_x;
	m_y = cpu.m_y;
	m_pc = cpu.m_pc;
	m_sp = cpu.m_sp;
	m_p = cpu.m_p;
	nmi_line = cpu.nmi_line;
	nmi_previous = cpu.nmi_previous;
	nmi_previous_need = cpu.nmi_previous_need;
	irq_line = cpu.irq_line;

	m_cycle_count = cpu.m_cycle_count;
	nmi_need = cpu.nmi_need;
	current_opcode = cpu.current_opcode;

	addr_low = cpu.addr_low;
	addr_high = cpu.addr_high;
	m_temp_low = cpu.m_temp_low;
	effective_addr = cpu.effective_addr;
	offset = cpu.offset;
	inst_complete = cpu.inst_complete;
	addr_complete = cpu.addr_complete;
	_operand = cpu._operand;
	cycle_state = cpu.cycle_state;  // Tracks the current micro-op (0, 1, 2...)
	page_crossed = cpu.page_crossed;

	prev_run_irq = cpu.prev_run_irq;
	run_irq = cpu.run_irq;
}

// ---------------- Debug helper ----------------
inline void NesCpu::dbg(const wchar_t* fmt, ...) {
#ifdef CPUDEBUG
	//if (!debug) return;
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
	va_end(args);
	OutputDebugStringW(buf);
#endif
}

inline void NesCpu::dbgNmi(const wchar_t* fmt, ...) {
#ifdef NMIDEBUG
	//if (!debug) return;
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
	va_end(args);
	OutputDebugStringW(buf);
#endif
}

void NesCpu::PowerCycle() {
	uint8_t resetLo = ReadByte(0xFFFC);
	uint8_t resHi = ReadByte(0xFFFD);
	m_pc = (static_cast<uint16_t>(resHi << 8)) | resetLo;
	m_p = FLAG_INTERRUPT | FLAG_UNUSED;
	m_a = 0;
	m_x = 0;
	m_y = 0;
	m_sp = 0xFD;
	m_cycle_count = 0;
	isActive = true;
	nmi_line = false;
	nmi_previous = false;
	irq_line = false;
	nmi_previous_need = false;
	nmi_need = false;
	isFrozen = false;
	current_opcode = 0x00;

	addr_low = 0;
	addr_high = 0;
	m_temp_low = 0;
	effective_addr = 0;
	offset = 0;
	inst_complete = true;
	addr_complete = false;
	_operand = 0;
	cycle_state = 0;  // Tracks the current micro-op (0, 1, 2...)
	page_crossed = 0;

	prev_run_irq = false;
	run_irq = false;
}

void NesCpu::Activate(bool active)
{
	isActive = active;
}

void NesCpu::Reset() {
	reset_line = true;
}

void NesCpu::AddCycles(int count)
{
	m_cycle_count += count;
}

uint64_t NesCpu::GetCycleCount()
{
	return m_cycle_count;
}

/// <summary>
/// Only used for testing purposes to trigger an NMI immediately.
/// </summary>
void NesCpu::SetNMIImmediate() {
	nmi_previous_need = true;
	nmi_previous = true;
	nmi_need = true;
	nmi_line = true;
}

// Called by mappers/devices to set interrupt lines
void NesCpu::setNMI(bool state) {
	nmi_line = state;
}

/// <summary>
/// Only used for testing purposes to trigger an IRQ immediately.
/// </summary>
void NesCpu::SetIRQImmediate() {
	run_irq = true;
	prev_run_irq = true;
}

void NesCpu::setIRQ(bool state) {
	irq_line = state;
}

void NesCpu::push(uint8_t value) {
	bus->write(0x0100 + m_sp, value);
	m_sp--;
}

uint8_t NesCpu::pull() {
	m_sp++;
	return bus->read(0x0100 + m_sp);
}

uint8_t NesCpu::GetSP() {
	return m_sp;
}

void NesCpu::SetSP(uint8_t sp) {
	m_sp = sp;
}

inline uint8_t NesCpu::ReadByte(uint16_t addr) {
	return bus->read(addr);
}

void NesCpu::WriteByte(uint16_t addr, uint8_t value) {
	bus->write(addr, value);
}

uint8_t NesCpu::GetStatus()
{
	return m_p;
}

void NesCpu::SetStatus(uint8_t status)
{
	m_p = status;
}

void NesCpu::ClearFlag(uint8_t flag)
{
	m_p &= ~flag;
}

inline void NesCpu::SetZero(uint8_t value)
{
	// Set/clear zero flag
	if (value == 0) {
		m_p |= FLAG_ZERO;
	}
	else {
		m_p &= ~FLAG_ZERO;
	}
}

inline void NesCpu::SetNegative(uint8_t value)
{
	// Set/clear negative flag (bit 7 of result)
	if (value & 0x80) {
		m_p |= FLAG_NEGATIVE;
	}
	else {
		m_p &= ~FLAG_NEGATIVE;
	}
}

inline void NesCpu::SetOverflow(bool condition)
{
	if (condition) {
		m_p |= FLAG_OVERFLOW;
	}
	else {
		m_p &= ~FLAG_OVERFLOW;
	}
}

inline void NesCpu::SetCarry(bool condition)
{
	if (condition) {
		m_p |= FLAG_CARRY;
	}
	else {
		m_p &= ~FLAG_CARRY;
	}
}

inline void NesCpu::SetDecimal(bool condition)
{
	if (condition) {
		m_p |= FLAG_DECIMAL;
	}
	else {
		m_p &= ~FLAG_DECIMAL;
	}
}

inline void NesCpu::SetInterrupt(bool condition)
{
	if (condition) {
		m_p |= FLAG_INTERRUPT;
	}
	else {
		m_p &= ~FLAG_INTERRUPT;
	}
}

inline void NesCpu::SetBreak(bool condition)
{
	if (condition) {
		m_p |= FLAG_BREAK;
	}
	else {
		m_p &= ~FLAG_BREAK;
	}
}

// Primarily used for testing purposes.
uint8_t NesCpu::GetA()
{
	return m_a;
}

void NesCpu::SetA(uint8_t a)
{
	m_a = a;
}

uint8_t NesCpu::GetX()
{
	return m_x;
}

void NesCpu::SetX(uint8_t x)
{
	m_x = x;
}

uint8_t NesCpu::GetY()
{
	return m_y;
}

void NesCpu::SetY(uint8_t y)
{
	m_y = y;
}

bool NesCpu::GetFlag(uint8_t flag)
{
	return (m_p & flag) == flag;
}

void NesCpu::SetFlag(uint8_t flag)
{
	m_p |= flag;
}

uint16_t NesCpu::GetPC() {
	return m_pc;
}

void NesCpu::SetPC(uint16_t address)
{
	m_pc = address;
}

void NesCpu::ConsumeCycle() {
	m_cycle_count++;
}

void NesCpu::init_cpu() {
	reset_line = false;
	// Halt on invalid instructions.
	for (int i = 0; i < 0x100; i++) {
		opcode_table[i] = &run_standalone_instruction<Op_HLT>;
	}

	opcode_table[0x06] = &run_instruction<Mode_ZeroPage, Op_RMW<Logic_ASL>>;
	opcode_table[0x0A] = &run_accumulator_instruction<Op_ASL>;
	opcode_table[0x0E] = &run_instruction<Mode_Absolute, Op_RMW<Logic_ASL>>;
	opcode_table[0x16] = &run_instruction<Mode_ZeroPageX, Op_RMW<Logic_ASL>>;
	opcode_table[0x1E] = &run_instruction<Mode_AbsoluteX<Op_ASL::is_rmw>, Op_RMW<Logic_ASL>>;

	opcode_table[0x21] = &run_instruction<Mode_IndirectX, Op_AND>;
	opcode_table[0x25] = &run_instruction<Mode_ZeroPage, Op_AND>;
	opcode_table[0x29] = &run_instruction<Mode_Immediate, Op_AND>;
	opcode_table[0x2D] = &run_instruction<Mode_Absolute, Op_AND>;
	opcode_table[0x31] = &run_instruction<Mode_IndirectY<Op_AND::is_rmw>, Op_AND>;
	opcode_table[0x35] = &run_instruction<Mode_ZeroPageX, Op_AND>;
	opcode_table[0x39] = &run_instruction<Mode_AbsoluteY<Op_AND::is_rmw>, Op_AND>;
	opcode_table[0x3D] = &run_instruction<Mode_AbsoluteX<Op_AND::is_rmw>, Op_AND>;

	opcode_table[0x61] = &run_instruction<Mode_IndirectX, Op_ADC>;
	opcode_table[0x65] = &run_instruction<Mode_ZeroPage, Op_ADC>;
	opcode_table[0x69] = &run_instruction<Mode_Immediate, Op_ADC>;
	opcode_table[0x6D] = &run_instruction<Mode_Absolute, Op_ADC>;
	opcode_table[0x71] = &run_instruction<Mode_IndirectY<Op_ADC::is_rmw>, Op_ADC>;
	opcode_table[0x75] = &run_instruction<Mode_ZeroPageX, Op_ADC>;
	opcode_table[0x79] = &run_instruction<Mode_AbsoluteY<Op_ADC::is_rmw>, Op_ADC>;
	opcode_table[0x7D] = &run_instruction<Mode_AbsoluteX<Op_ADC::is_rmw>, Op_ADC>;

	opcode_table[0x90] = &run_standalone_instruction<Op_BCC>;
	opcode_table[0xB0] = &run_standalone_instruction<Op_BCS>;
	opcode_table[0xF0] = &run_standalone_instruction<Op_BEQ>;
	opcode_table[0x30] = &run_standalone_instruction<Op_BMI>;
	opcode_table[0xD0] = &run_standalone_instruction<Op_BNE>;
	opcode_table[0x10] = &run_standalone_instruction<Op_BPL>;
	opcode_table[0x50] = &run_standalone_instruction<Op_BVC>;
	opcode_table[0x70] = &run_standalone_instruction<Op_BVS>;

	opcode_table[0x00] = &run_standalone_instruction<Op_BRK>;

	opcode_table[0x24] = &run_instruction<Mode_ZeroPage, Op_BIT>;
	opcode_table[0x2C] = &run_instruction<Mode_Absolute, Op_BIT>;

	opcode_table[0x18] = &run_instruction<Mode_Implied, Op_CLC>;
	opcode_table[0xD8] = &run_instruction<Mode_Implied, Op_CLD>;
	opcode_table[0x58] = &run_instruction<Mode_Implied, Op_CLI>;
	opcode_table[0xB8] = &run_instruction<Mode_Implied, Op_CLV>;

	opcode_table[0xC9] = &run_instruction<Mode_Immediate, Op_CMP>;
	opcode_table[0xC5] = &run_instruction<Mode_ZeroPage, Op_CMP>;
	opcode_table[0xD5] = &run_instruction<Mode_ZeroPageX, Op_CMP>;
	opcode_table[0xCD] = &run_instruction<Mode_Absolute, Op_CMP>;
	opcode_table[0xDD] = &run_instruction<Mode_AbsoluteX<Op_CMP::is_rmw>, Op_CMP>;
	opcode_table[0xD9] = &run_instruction<Mode_AbsoluteY<Op_CMP::is_rmw>, Op_CMP>;
	opcode_table[0xC1] = &run_instruction<Mode_IndirectX, Op_CMP>;
	opcode_table[0xD1] = &run_instruction<Mode_IndirectY<Op_CMP::is_rmw>, Op_CMP>;

	opcode_table[0xE0] = &run_instruction<Mode_Immediate, Op_CPX>;
	opcode_table[0xE4] = &run_instruction<Mode_ZeroPage, Op_CPX>;
	opcode_table[0xEC] = &run_instruction<Mode_Absolute, Op_CPX>;

	opcode_table[0xC0] = &run_instruction<Mode_Immediate, Op_CPY>;
	opcode_table[0xC4] = &run_instruction<Mode_ZeroPage, Op_CPY>;
	opcode_table[0xCC] = &run_instruction<Mode_Absolute, Op_CPY>;

	opcode_table[0xC6] = &run_instruction<Mode_ZeroPage, Op_RMW<Logic_DEC>>;
	opcode_table[0xD6] = &run_instruction<Mode_ZeroPageX, Op_RMW<Logic_DEC>>;
	opcode_table[0xCE] = &run_instruction<Mode_Absolute, Op_RMW<Logic_DEC>>;
	opcode_table[0xDE] = &run_instruction<Mode_AbsoluteX<true>, Op_RMW<Logic_DEC>>;

	opcode_table[0xCA] = &run_instruction<Mode_Implied, Op_DEX>;
	opcode_table[0x88] = &run_instruction<Mode_Implied, Op_DEY>;

	opcode_table[0x49] = &run_instruction<Mode_Immediate, Op_EOR>;
	opcode_table[0x45] = &run_instruction<Mode_ZeroPage, Op_EOR>;
	opcode_table[0x55] = &run_instruction<Mode_ZeroPageX, Op_EOR>;
	opcode_table[0x4D] = &run_instruction<Mode_Absolute, Op_EOR>;
	opcode_table[0x5D] = &run_instruction<Mode_AbsoluteX<Op_EOR::is_write>, Op_EOR>;
	opcode_table[0x59] = &run_instruction<Mode_AbsoluteY<Op_EOR::is_write>, Op_EOR>;
	opcode_table[0x41] = &run_instruction<Mode_IndirectX, Op_EOR>;
	opcode_table[0x51] = &run_instruction<Mode_IndirectY<Op_EOR::is_write>, Op_EOR>;

	opcode_table[0xE6] = &run_instruction<Mode_ZeroPage, Op_RMW<Logic_INC>>;
	opcode_table[0xF6] = &run_instruction<Mode_ZeroPageX, Op_RMW<Logic_INC>>;
	opcode_table[0xEE] = &run_instruction<Mode_Absolute, Op_RMW<Logic_INC>>;
	// $FE: INC Absolute, X
	// Uses "is_rmw=true" -> Forces 7 cycles (Mode T1-T3, Op T4-T6)
	opcode_table[0xFE] = &run_instruction<Mode_AbsoluteX<true>, Op_RMW<Logic_INC>>;
	opcode_table[0xE8] = &run_instruction<Mode_Implied, Op_INX>;
	opcode_table[0xC8] = &run_instruction<Mode_Implied, Op_INY>;

	opcode_table[0x4C] = &run_standalone_instruction<Op_JMP_Absolute>;
	opcode_table[0x6C] = &run_standalone_instruction<Op_JMP_Indirect>;

	opcode_table[0x20] = &run_standalone_instruction<Op_JSR>;

	opcode_table[0xA9] = &run_instruction<Mode_Immediate, Op_LDA>;
	opcode_table[0xA5] = &run_instruction<Mode_ZeroPage, Op_LDA>;
	opcode_table[0xB5] = &run_instruction<Mode_ZeroPageX, Op_LDA>;
	opcode_table[0xAD] = &run_instruction<Mode_Absolute, Op_LDA>;
	opcode_table[0xBD] = &run_instruction<Mode_AbsoluteX<Op_LDA::is_rmw>, Op_LDA>;
	opcode_table[0xB9] = &run_instruction<Mode_AbsoluteY<Op_LDA::is_rmw>, Op_LDA>;
	opcode_table[0xA1] = &run_instruction<Mode_IndirectX, Op_LDA>;
	opcode_table[0xB1] = &run_instruction<Mode_IndirectY<Op_LDA::is_rmw>, Op_LDA>;

	opcode_table[0xA2] = &run_instruction<Mode_Immediate, Op_LDX>;
	opcode_table[0xA6] = &run_instruction<Mode_ZeroPage, Op_LDX>;
	opcode_table[0xB6] = &run_instruction<Mode_ZeroPageY, Op_LDX>;
	opcode_table[0xAE] = &run_instruction<Mode_Absolute, Op_LDX>;
	opcode_table[0xBE] = &run_instruction<Mode_AbsoluteY<Op_LDX::is_rmw>, Op_LDX>;

	opcode_table[0xA0] = &run_instruction<Mode_Immediate, Op_LDY>;
	opcode_table[0xA4] = &run_instruction<Mode_ZeroPage, Op_LDY>;
	opcode_table[0xB4] = &run_instruction<Mode_ZeroPageX, Op_LDY>;
	opcode_table[0xAC] = &run_instruction<Mode_Absolute, Op_LDY>;
	opcode_table[0xBC] = &run_instruction<Mode_AbsoluteX<Op_LDY::is_rmw>, Op_LDY>;

	opcode_table[0x4A] = &run_instruction<Mode_Implied, Op_LSR_Accumulator>;
	opcode_table[0x46] = &run_instruction<Mode_ZeroPage, Op_RMW<Logic_LSR>>;
	opcode_table[0x56] = &run_instruction<Mode_ZeroPageX, Op_RMW<Logic_LSR>>;
	opcode_table[0x4E] = &run_instruction<Mode_Absolute, Op_RMW<Logic_LSR>>;
	opcode_table[0x5E] = &run_instruction<Mode_AbsoluteX<true>, Op_RMW<Logic_LSR>>;

	opcode_table[0xEA] = &run_instruction<Mode_Implied, Op_NOP>;

	opcode_table[0x09] = &run_instruction<Mode_Immediate, Op_ORA>;
	opcode_table[0x05] = &run_instruction<Mode_ZeroPage, Op_ORA>;
	opcode_table[0x15] = &run_instruction<Mode_ZeroPageX, Op_ORA>;
	opcode_table[0x0D] = &run_instruction<Mode_Absolute, Op_ORA>;
	opcode_table[0x1D] = &run_instruction<Mode_AbsoluteX<Op_ORA::is_rmw>, Op_ORA>;
	opcode_table[0x19] = &run_instruction<Mode_AbsoluteY<Op_ORA::is_rmw>, Op_ORA>;
	opcode_table[0x01] = &run_instruction<Mode_IndirectX, Op_ORA>;
	opcode_table[0x11] = &run_instruction<Mode_IndirectY<Op_ORA::is_rmw>, Op_ORA>;

	opcode_table[0x48] = &run_standalone_instruction<Op_PHA>;
	opcode_table[0x08] = &run_standalone_instruction<Op_PHP>;
	opcode_table[0x68] = &run_standalone_instruction<Op_PLA>;
	opcode_table[0x28] = &run_standalone_instruction<Op_PLP>;

	opcode_table[0x2A] = &run_instruction<Mode_Implied, Op_ROL_Accumulator>;
	opcode_table[0x26] = &run_instruction<Mode_ZeroPage, Op_RMW<Logic_ROL>>;
	opcode_table[0x36] = &run_instruction<Mode_ZeroPageX, Op_RMW<Logic_ROL>>;
	opcode_table[0x2E] = &run_instruction<Mode_Absolute, Op_RMW<Logic_ROL>>;
	opcode_table[0x3E] = &run_instruction<Mode_AbsoluteX<true>, Op_RMW<Logic_ROL>>;

	opcode_table[0x6A] = &run_instruction<Mode_Implied, Op_ROR_Accumulator>;
	opcode_table[0x66] = &run_instruction<Mode_ZeroPage, Op_RMW<Logic_ROR>>;
	opcode_table[0x76] = &run_instruction<Mode_ZeroPageX, Op_RMW<Logic_ROR>>;
	opcode_table[0x6E] = &run_instruction<Mode_Absolute, Op_RMW<Logic_ROR>>;
	opcode_table[0x7E] = &run_instruction<Mode_AbsoluteX<true>, Op_RMW<Logic_ROR>>;


	opcode_table[0x40] = &run_standalone_instruction<Op_RTI>;
	opcode_table[0x60] = &run_standalone_instruction<Op_RTS>;

	opcode_table[0xE1] = &run_instruction<Mode_IndirectX, Op_SBC>;
	opcode_table[0xE5] = &run_instruction<Mode_ZeroPage, Op_SBC>;
	opcode_table[0xE9] = &run_instruction<Mode_Immediate, Op_SBC>;
	opcode_table[0xED] = &run_instruction<Mode_Absolute, Op_SBC>;
	opcode_table[0xF1] = &run_instruction<Mode_IndirectY<Op_SBC::is_rmw>, Op_SBC>;
	opcode_table[0xF5] = &run_instruction<Mode_ZeroPageX, Op_SBC>;
	opcode_table[0xF9] = &run_instruction<Mode_AbsoluteY<Op_SBC::is_rmw>, Op_SBC>;
	opcode_table[0xFD] = &run_instruction<Mode_AbsoluteX<Op_SBC::is_rmw>, Op_SBC>;

	opcode_table[0x38] = &run_instruction<Mode_Implied, Op_SEC>;
	opcode_table[0xF8] = &run_instruction<Mode_Implied, Op_SED>;
	opcode_table[0x78] = &run_instruction<Mode_Implied, Op_SEI>;

	opcode_table[0x81] = &run_instruction<Mode_IndirectX, Op_STA>;
	opcode_table[0x85] = &run_instruction<Mode_ZeroPage, Op_STA>;
	opcode_table[0x8D] = &run_instruction<Mode_Absolute, Op_STA>;
	opcode_table[0x91] = &run_instruction<Mode_IndirectY<Op_STA::is_rmw>, Op_STA>;
	opcode_table[0x95] = &run_instruction<Mode_ZeroPageX, Op_STA>;
	opcode_table[0x99] = &run_instruction<Mode_AbsoluteY<Op_STA::is_rmw>, Op_STA>;
	opcode_table[0x9D] = &run_instruction<Mode_AbsoluteX<Op_STA::is_rmw>, Op_STA>;

	opcode_table[0x86] = &run_instruction<Mode_ZeroPage, Op_STX>;
	opcode_table[0x96] = &run_instruction<Mode_ZeroPageY, Op_STX>;
	opcode_table[0x8E] = &run_instruction<Mode_Absolute, Op_STX>;

	opcode_table[0x84] = &run_instruction<Mode_ZeroPage, Op_STY>;
	opcode_table[0x94] = &run_instruction<Mode_ZeroPageX, Op_STY>;
	opcode_table[0x8C] = &run_instruction<Mode_Absolute, Op_STY>;

	opcode_table[0xAA] = &run_instruction<Mode_Implied, Op_TAX>;
	opcode_table[0xA8] = &run_instruction<Mode_Implied, Op_TAY>;
	opcode_table[0xBA] = &run_instruction<Mode_Implied, Op_TSX>;
	opcode_table[0x8A] = &run_instruction<Mode_Implied, Op_TXA>;
	opcode_table[0x9A] = &run_instruction<Mode_Implied, Op_TXS>;
	opcode_table[0x98] = &run_instruction<Mode_Implied, Op_TYA>;

	// Phantom op codes
	opcode_table[OP_NMI] = &run_standalone_instruction<Op_HardwareInterrupt_NMI>;
	opcode_table[OP_IRQ] = &run_standalone_instruction<Op_HardwareInterrupt_IRQ>;
	opcode_table[OP_RESET] = &run_standalone_instruction<Op_HardwareReset>;

	// Unnoficial op codes
	opcode_table[0x04] = &run_instruction<Mode_ZeroPage, Op_NOP>;
}