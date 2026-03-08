#include "MMC3.h"
#include "CPU.h"
#include "Cartridge.h"
#include "Bus.h"
#include "PPU.h"
#include "RendererLoopy.h"
#include "Mapper.h"

#define BANK_SIZE_CHR 0x400 // 1KB
#define BANK_SIZE_PRG 0x2000 // 8KB
static constexpr int A12_LOW_THRESHOLD = 3; // require A12 low >= this many CPU cycles

// ---------------- Debug helper ----------------
inline void MMC3::dbg(const wchar_t* fmt, ...) {
#ifdef MMC3DEBUG
	//if (!debug) return;
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
	va_end(args);
	OutputDebugStringW(buf);
#endif
}

MMC3::MMC3(Bus& b, uint8_t prgRomSize, uint8_t chrRomSize) : bus(b), cpu(b.cpu) {
	MapperBase::SetPrgPageSize(0x2000);
	MapperBase::SetChrPageSize(0x400);
	renderLoopy = bus.ppu.renderer;
	// init registers to reset-like defaults
	prgMode = 0;
	chrMode = 0;
	prgBank16kCount = prgRomSize;
	prgBank8kCount = prgBank16kCount * 2;
	chrBank8kCount = chrRomSize;
	chrBank1kCount = chrBank8kCount * 8;
	for (int i = 0; i < 8; i++) {
		banks[i] = 0;
	}

	irq_latch = 0;
	irq_counter = 0;
	irq_reload = false;
	irq_enabled = false;
	last_a12 = false;
	_irqPending = false;
	renderLoopy->setMapper(this);
	this->bus.ppu.setMapper(this);

	this->cart = &bus.cart;
	uint32_t lastBankStart = (prgBank8kCount - 1) * BANK_SIZE_PRG;
	//lastPrg = &cartridge->m_prgRomData[lastBankStart];
	//secondLastPrg = &cartridge->m_prgRomData[lastBankStart - BANK_SIZE_PRG];

}

MMC3::~MMC3() {

}

void MMC3::initialize(ines_file_t& data) {
	if (data.header.flags6 & FLAG_6_NAMETABLE_LAYOUT) {
		isFourScreen = true;
		_nametableRamSize = 0x1000; // 4 screen
	}
	MapperBase::initialize(data);
	MapperBase::SetPrgPageSize(0x2000);
	MapperBase::SetChrPageSize(0x400);
	if (data.header.flags6 & FLAG_6_NAMETABLE_LAYOUT) {
		MapperBase::SetMirrorMode(MapperBase::MirrorMode::FOUR_SCREEN);
	}
}

void MMC3::shutdown() {
	renderLoopy->setMapper(nullptr);
	this->bus.ppu.setMapper(nullptr);
}

void MMC3::RecomputeChrMappings() {
	if (chrMode) {
		// $0000-$03FF: R2 (fine)
		// $0400-$07FF: R3
		// $0800-$0BFF: R4
		// $0C00-$0FFF: R5
		// $1000-$13FF: R0 (2KB split)
		// $1400-$17FF: R0 + 1KB
		// $1800-$1BFF: R1 (2KB split)
		// $1C00-$1FFF: R1 + 1KB

		MapperBase::SetChrPage(0, banks[2]);
		MapperBase::SetChrPage(1, banks[3]);
		MapperBase::SetChrPage(2, banks[4]);
		MapperBase::SetChrPage(3, banks[5]);
		MapperBase::SetChrPage(4, banks[0]);
		MapperBase::SetChrPage(5, banks[0] + 1);
		MapperBase::SetChrPage(6, banks[1]);
		MapperBase::SetChrPage(7, banks[1] + 1);
	}
	else {
		// R0 covers $0000-$0FFF (two 1KB slots)
		// R1 covers $1000-$1FFF
		MapperBase::SetChrPage(0, banks[0]);
		MapperBase::SetChrPage(1, banks[0] + 1);
		MapperBase::SetChrPage(2, banks[1]);
		MapperBase::SetChrPage(3, banks[1] + 1);
		MapperBase::SetChrPage(4, banks[2]);
		MapperBase::SetChrPage(5, banks[3]);
		MapperBase::SetChrPage(6, banks[4]);
		MapperBase::SetChrPage(7, banks[5]);
	}
}

void MMC3::RecomputePrgMappings() {
	if (prgMode == 0) {
		MapperBase::SetPrgPage(0, banks[6]); // 8000
		MapperBase::SetPrgPage(1, banks[7]); // A000
		MapperBase::SetPrgPage(2, prgBank8kCount - 2); // C000
		MapperBase::SetPrgPage(3, prgBank8kCount - 1); // E000
	}
	else {
		MapperBase::SetPrgPage(0, prgBank8kCount - 2); // 8000
		MapperBase::SetPrgPage(1, banks[7]); // A000
		MapperBase::SetPrgPage(2, banks[6]); // C000
		MapperBase::SetPrgPage(3, prgBank8kCount - 1); // E000
	}
}

void MMC3::writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) {
	LOG(L"(%d) 0x%04X 0x%02X (%d) \n", currentCycle, addr, val, val);
	switch (addr & 0xE001) {
		case 0x8000:
			// Control register
			prgMode = val & 0x40;
			chrMode = val & 0x80;
			m_regSelect = val & 0x7; // 3 LSB
			//LOG(L"prg: %d chr: %d reg: %d\n", prgMode, chrMode, m_regSelect);
			RecomputeMappings();
			break;

		case 0x8001:
			banks[m_regSelect] = val;
			if (m_regSelect == 2 && val == 0xC7) {
				int i = 0;
			}

			if (m_regSelect <= 1) {
				banks[m_regSelect] &= ~1;      // R0/R1: ignore bit 0
			}
			if (m_regSelect >= 6) {
				banks[m_regSelect] &= 0x3F;   // R6/R7: 6-bit
			}

			if (m_regSelect < 6) {
				banks[m_regSelect] &= chrBank1kCount - 1;
				RecomputeChrMappings();
			}
			else {
				RecomputePrgMappings();
			}
			break;

		case 0xA000:
			if (isFourScreen) {
				SetMirrorMode(MapperBase::MirrorMode::FOUR_SCREEN);
			}
			else {
				SetMirrorMode((val & 1) == 0 ? MapperBase::MirrorMode::VERTICAL : MapperBase::MirrorMode::HORIZONTAL);
			}
			break;

		case 0xC000:  // IRQ Latch
			irq_latch = val;
			break;

		case 0xC001:  // IRQ Reload
			irq_reload = true;
			break;

		case 0xE000:  // IRQ Disable
			irq_enabled = false;
			acknowledgeIRQ();
			break;

		case 0xE001:  // IRQ Enable
			irq_enabled = true;
			break;
	}
}

void MMC3::triggerIRQ() {
	_irqPending = true;
}

void MMC3::acknowledgeIRQ() {
	_irqPending = false;
}

bool MMC3::IrqPending() {
	return _irqPending;
}

// Called by PPU when PPU address changes (A12 detection)
void MMC3::ClockIRQCounter(uint16_t ppu_address) {
	if (ppu_address >= 0x2000) {
		return;
	}
	bool current_a12 = (ppu_address & 0x1000) != 0;

	// Track low-time duration
	if (current_a12) {
		// Detect rising edge of A12 (0 -> 1 transition)
		if (!last_a12 && current_a12 && (cpu.GetCycleCount() - a12LowCycle >= A12_LOW_THRESHOLD)) {
		//if (!last_a12 && current_a12) {
			//LOG(L"Scanline (%d) detected, dec %d \n", bus->ppu->renderer->m_scanline, irq_counter);
			//if (a12_filter == 0) {
				// Clock the counter on rising edge
			if (irq_counter == 0 || irq_reload) {
				irq_counter = irq_latch;
				irq_reload = false;
			}
			else {
				//LOG(L"IRQ counter decrement %d\n", irq_counter);
				irq_counter--;
			}

			// Trigger IRQ when counter reaches 0
			if (irq_counter == 0 && irq_enabled) {
				//LOG(L"IRQ Triggered at Scanline: %d, Dot: %d\n", bus.ppu.renderer->m_scanline, bus.ppu.renderer->dot);
				triggerIRQ();
			}

			LOG(L"A12 Edge Detected: Scanline: %d, Dot: %d, IRQ Counter: %d\n", bus.ppu.renderer->m_scanline, bus.ppu.renderer->dot, irq_counter);

			//a12_filter = 6;  // Typical filter delay
		//}
		}
		a12LowCycle = cpu.GetCycleCount();
	}

	last_a12 = current_a12;

	//if (a12_filter > 0) {
	//	a12_filter--;
	//}
}

void MMC3::Serialize(Serializer& serializer) {
	MapperBase::Serialize(serializer);
	serializer.Write(prgMode);
	serializer.Write(chrMode);
	serializer.Write(banks, 8);
	serializer.Write(m_regSelect);
	// IRQ state
	serializer.Write(irq_latch);
	serializer.Write(irq_counter);
	serializer.Write(irq_reload);
	serializer.Write(irq_enabled);
	// A12 tracking
	serializer.Write(last_a12);
	serializer.Write(a12LowCycle);
	serializer.Write(_irqPending);
}

void MMC3::Deserialize(Serializer& serializer) {
	MapperBase::Deserialize(serializer);
	serializer.Read(prgMode);
	serializer.Read(chrMode);
	serializer.Read(banks, 8);
	serializer.Read(m_regSelect);
	// IRQ state
	serializer.Read(irq_latch);
	serializer.Read(irq_counter);
	serializer.Read(irq_reload);
	serializer.Read(irq_enabled);
	// A12 tracking
	serializer.Read(last_a12);
	serializer.Read(a12LowCycle);
	serializer.Read(_irqPending);
	RecomputeMappings();
}