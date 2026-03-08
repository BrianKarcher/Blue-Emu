#include "MMC1.h"
#include "Cartridge.h"
#include <Windows.h>
#include <string>
#include <bitset>
#include "CPU.h"

MMC1::MMC1(Cartridge* cartridge, CPU& c) : cpu(c) {
	MapperBase::SetPrgPageSize(0x4000);
	MapperBase::SetChrPageSize(0x1000);
	// We use the 1 bit to track when the register is full, instead of a separate counter.
	shiftRegister = 0b10000;
	// init registers to reset-like defaults
	controlReg = 0x0C; // PRG ROM mode = 3 (fix last bank), CHR mode = 0, mirroring = 0 (single-screen lower)
	chrBank0Reg = 0;
	chrBank1Reg = 0;
	prgBankReg = 0; // MMC1 starts Bank 0 at $8000
	this->cartridge = cartridge;
}

void MMC1::initialize(ines_file_t& data) {
	prgBank16kCount = data.header.prg_rom_size;
	suromPrgOuterBank = 0;
	// Bank counts are in 4KB's, chr_rom_size is in 8KB units.
	chrBankCount = data.header.chr_rom_size * 2;
	// Detect board type from PRG/CHR configuration
	if (prgBank16kCount == 4 && chrBankCount == 4) {
		boardType = BoardType::SAROM;   // 64KB PRG, 16KB CHR-ROM
	}
	else if (prgBank16kCount == 8 && chrBankCount == 0) {
		boardType = BoardType::SNROM;   // 128KB PRG, CHR-RAM
	}
	else if (prgBank16kCount == 32) {
		boardType = BoardType::SUROM;   // 512KB PRG, 0KB CHR-ROM
	}
	else {
		boardType = BoardType::GenericMMC1;
	}
	MapperBase::initialize(data);
}

// ---------------- Debug helper ----------------
inline void MMC1::dbg(const wchar_t* fmt, ...) const {
	if (!debug) return;
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
	va_end(args);
	OutputDebugStringW(buf);
}

void MMC1::writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) {
	// Ignore writes that happen too close together (within 2 CPU cycles)
	// "On consecutive-cycle writes, writes to the shift register (D0) after the first are ignored."
	if (currentCycle > 0 && (currentCycle - lastWriteCycle) < 2) {
		LOG(L"MMC1: Ignoring consecutive-cycle write\n");
		return;
	}
	lastWriteCycle = currentCycle;
	// Debug print
	//std::string bits = std::bitset<8>(shiftRegister).to_string();
	//std::wstring wbits(bits.begin(), bits.end());
	//LOG(L"MMC1 write 0x%04X = 0x%02X  shiftReg=0b%s\n",
	//	addr, val, wbits.c_str());
	//std::wstring bits = std::to_wstring(std::bitset<8>(shiftRegister).to_string());
	LOG(L"MMC 0x%08X %S %S\n", addr, std::bitset<8>(val).to_string().c_str(), std::bitset<8>(shiftRegister).to_string().c_str());  // 8-digit uppercase hex
	//OutputDebugStringW((L"MMC " + std::to_wstring(addr) + L" " + std::to_wstring(val) + L" " + std::to_wstring(shiftRegister) + L"\n").c_str());
	// High bit resets the shift register.
	if (val & 0x80) {
		shiftRegister = 0b10000;
		//prgBankReg = 2; // MMC1 starts Bank 0 at $8000
		controlReg |= 0x0C; // set PRG mode bits to 11 (mode 3) as hardware typically does on reset
		LOG(L"MMC1 reset: ctrl=0x%02X addr=0x%04X val=%d\n", controlReg, addr, val);
		RecomputeMappings();
		return;
	}

	// Shift right
	bool full = shiftRegister & 1;  // Check BEFORE shift
	shiftRegister >>= 1;
	shiftRegister |= ((val & 1) << 4);

	if (full) {
		// Now we have 5 bits
		processShift(addr, shiftRegister & 0x1F); // the 5-bit value
		shiftRegister = 0b10000;
	}

	//if ((shiftRegister & 1) == 1) {
	//	// All five writes are in!
	//	shiftRegister >>= 1;
	//	// Record the last value that fills the register.
	//	// Record one bit at a time.
	//	shiftRegister |= ((val & 1) << 4);
	//	wchar_t buffer2[60];
	//	swprintf_s(buffer2, L"Shift full, processing %S to 0x%08X\n", std::bitset<8>(shiftRegister).to_string().c_str(), addr);  // 8-digit uppercase hex
	//	OutputDebugStringW(buffer2);
	//	//OutputDebugStringW((L"Shift full, processing " + std::to_wstring(val) + L" to " + std::to_wstring(addr)).c_str());
	//	processShift(addr, shiftRegister);
	//	shiftRegister = 0b10000;
	//}
	//else {
	//	shiftRegister >>= 1;
	//	shiftRegister |= ((val & 1) << 4);
	//}
}

void MMC1::processShift(uint16_t addr, uint8_t val) {
	// Control register
	std::string bits = std::bitset<8>(val).to_string();
	std::wstring wbits(bits.begin(), bits.end());
	LOG(L"MMC1 shift full for 0x%04X -> data=0b%s (0x%02X)\n",
		addr, wbits.c_str(), val);
	if (addr >= 0x8000 && addr <= 0x9FFF) {
		// Control register (mirroring + PRG mode + CHR mode)
		controlReg = val & 0x1F;
		
		uint8_t nt = val & 0b11;
		if (nt == 0) {
			SetMirrorMode(MapperBase::MirrorMode::SINGLE_LOWER);
		}
		else if (nt == 1) {
			SetMirrorMode(MapperBase::MirrorMode::SINGLE_UPPER);
		}
		else if (nt == 2) {
			SetMirrorMode(MapperBase::MirrorMode::VERTICAL);
		}
		else {
			SetMirrorMode(MapperBase::MirrorMode::HORIZONTAL);
		}
		
	}
	// CHR Bank 0
	else if (addr >= 0xA000 && addr <= 0xBFFF) {
		// CHR bank 0 (4KB)
		if (val > 3) {
			int i = 0;
		}
		const uint8_t chrMode = (controlReg >> 4) & 0x01; // bit 4
		// Low bit ignored in 8KB mode
		if (chrMode == 0) {
			chrBank0Reg = val & 0x1E;
		}
		else {
			chrBank0Reg = val & 0x1F;
		}
		
		if (boardType == BoardType::SUROM) {
			suromPrgOuterBank = (val & 0x10);
			LOG(L"SUROM: CHR bank 0 set, outer PRG bank = %d\n", suromPrgOuterBank ? 1 : 0);
		}
	}
	// CHR Bank 1
	else if (addr >= 0xC000 && addr <= 0xDFFF) {
		const uint8_t chrMode = (controlReg >> 4) & 0x01; // bit 4
		// ignored in 8KB mode
		if (chrMode == 1) {
			// CHR bank 1 (4KB)
			chrBank1Reg = val & 0x1F;
			if (boardType == BoardType::SUROM) {
				suromPrgOuterBank = (val & 0x10);
				LOG(L"SUROM: CHR bank 1 set, outer PRG bank = %d\n", suromPrgOuterBank ? 1 : 0);
			}
		}
	}
	// PRG Bank
	else if (addr >= 0xE000 && addr <= 0xFFFF) {
		// PRG bank register
		//LOG(L"\nChanging prg bank to %d\n", val & 0x0F);
		cartridge->SetPrgRamEnabled((val & 0b10000) == 0 ? true : false);
		prgBankReg = val & 0x0F; // only low 4 bits used for PRG bank
	}
	// --- Clamp CHR bank values for SAROM ---
	//if (boardType == BoardType::SAROM) {
	//	// SAROM only has 16KB of CHR-ROM
	//	chrBank0Reg &= 0x04;
	//	chrBank1Reg &= 0x04;
	//}
	RecomputeMappings();
}

void MMC1::RecomputePrgMappings() {
	const uint8_t prgMode = (controlReg >> 2) & 0x03; // bits 2-3

	// ------------ PRG BANKING ------------
	uint32_t prgMax = prgBank16kCount - 1;
	uint32_t prgBank = prgBankReg; // &prgMax;
	uint32_t lastBankStart = 0;
	if (boardType == BoardType::SUROM) {
		// SUROM has 512KB PRG-ROM (32 x 16KB banks), but only 16 banks are selectable
		prgBank &= 15;
		// SUROM uses the outer bank bit from the CHR bank register to select high bit of PRG bank
		prgBank |= suromPrgOuterBank; // set bit 4 from CHR bank reg bit 4
		lastBankStart = suromPrgOuterBank ? 31 : 15;
	}
	else {
		prgBank %= prgMax + 1;
		lastBankStart = prgBank16kCount - 1;
	}

	switch (prgMode) {

	case 0:
	case 1:
		// 32 KB mode
	{
		uint32_t bank32k = (prgBank & 0xFE); // even bank
		MapperBase::SetPrgPage(0, bank32k);
		MapperBase::SetPrgPage(1, bank32k + 1);
	}
	break;

	case 2:
		// First 16KB fixed at $8000
		MapperBase::SetPrgPage(0, 0);
		MapperBase::SetPrgPage(1, prgBank);
		break;

	case 3:
	default:
		// Last 16KB fixed at $C000
		MapperBase::SetPrgPage(0, prgBank);
		MapperBase::SetPrgPage(1, lastBankStart);
		break;
	}

	LOG(L"MMC1 recompute: control=0x%02X prgMode=%d chrMode=%d\n", controlReg, (controlReg >> 2) & 3, (controlReg >> 4) & 1);
	//LOG(L"  PRG addrs: prg0=0x%06X(0x%02X) prg1=0x%06X(0x%02X) (prgBankCount=%d)\n", prg0Addr, prg0Addr / 0x4000, prg1Addr, prg1Addr / 0x4000, prgBank16kCount);
	//LOG(L"  CHR addrs: chr0=0x%06X chr1=0x%06X (chrBankCount=%d)\n", chr0Addr, chr1Addr, chrBankCount);
}

void MMC1::RecomputeChrMappings() {
	const uint8_t chrMode = (controlReg >> 4) & 0x01; // bit 4

	// -------- CHR mapping --------
	// Remove the early return - always compute banking even for CHR-RAM!
	if (chrBankCount == 0) {
		// CHR-RAM: always 8KB, no real banking
		MapperBase::SetChrPage(0, 0);
		MapperBase::SetChrPage(1, 1);
	}
	else if (chrMode == 0) {
		// 8KB mode
		uint32_t bank8k = chrBank0Reg;
		//if (boardType == BoardType::SAROM)
		//	bank8k &= 0x04;                     // SAROM: only 4 banks (16KB total)
		bank8k %= chrBankCount;
		MapperBase::SetChrPage(0, bank8k);
		MapperBase::SetChrPage(1, bank8k + 1);
	}
	else {
		// 4KB mode
		uint32_t bank0 = chrBank0Reg; // &(chrBankCount - 1);
		uint32_t bank1 = chrBank1Reg; // &(chrBankCount - 1);
		MapperBase::SetChrPage(0, bank0);
		MapperBase::SetChrPage(1, bank1);
	}
}

void MMC1::Serialize(Serializer& serializer) {
	MapperBase::Serialize(serializer);
	serializer.Write(shiftRegister);
	serializer.Write(controlReg);
	serializer.Write(chrBank0Reg);
	serializer.Write(chrBank1Reg);
	serializer.Write(prgBankReg);
	serializer.Write(suromPrgOuterBank);

}

void MMC1::Deserialize(Serializer& serializer) {
	MapperBase::Deserialize(serializer);
	serializer.Read(shiftRegister);
	serializer.Read(controlReg);
	serializer.Read(chrBank0Reg);
	serializer.Read(chrBank1Reg);
	serializer.Read(prgBankReg);
	serializer.Read(suromPrgOuterBank);
	RecomputeMappings();
}