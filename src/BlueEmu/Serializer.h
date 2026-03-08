#pragma once
#include <cstdint>
#include <iostream>
#include <vector>
#include <array>

struct HeaderState {


};

struct CPUState {
	uint8_t m_a;
	uint8_t m_x;
	uint8_t m_y;
	int m_pc;
	uint8_t m_sp;
	uint8_t m_p;
	uint64_t m_cycle_count;

	uint16_t  current_opcode;
	uint8_t  addr_low;
	uint8_t  addr_high;
	uint8_t  m_temp_low;
	uint16_t effective_addr;
	int8_t offset;
	bool inst_complete = true;
	bool addr_complete = false;
	uint8_t _operand;
	int cycle_state;  // Tracks the current micro-op (0, 1, 2...)
	bool page_crossed;

	// Interrupt lines
	bool nmi_line;
	bool nmi_previous;
	bool nmi_previous_need;
	bool nmi_need;

	bool prev_run_irq;
	bool run_irq;
	bool irq_line;
};

struct InternalMemoryState {
	uint8_t internalMemory[2048];
};

struct LoopyState {
	uint16_t coarse_x;  // Coarse X scroll (0-31)
	uint16_t coarse_y;  // Coarse Y scroll (0-31)
	uint16_t nametable_x;  // Nametable X bit
	uint16_t nametable_y;  // Nametable Y bit
	uint16_t fine_y;    // Fine Y scroll (0-7)
	uint16_t unused;    // Always 0
};

struct SpriteState {
	uint8_t x;
	uint8_t y;
	uint8_t tileIndex;
	uint8_t attributes;
	bool isSprite0;
};

struct RendererState {
	int m_scanline;
	LoopyState v;
	LoopyState t;
	uint8_t x;
	bool w;
	uint16_t pattern_lo_shift;
	uint16_t pattern_hi_shift;
	uint16_t attr_lo_shift;
	uint16_t attr_hi_shift;
	uint64_t _frameCount; // We may not need this, it's to do odd/even frame handling
	uint8_t ppumask = 0;
	int dot = 0;
	SpriteState secondaryOAM[8];
	uint8_t spritePatternTableLow[8];
	uint8_t spritePatternTableHigh[8];
	uint16_t spritePatternAddrLow[8];
	uint16_t spritePatternAddrHigh[8];
};

struct PPUState {
	uint8_t oam[0x100];
	uint8_t oamAddr;
	uint8_t paletteTable[32];
	uint8_t ppuMask;
	uint8_t ppuStatus;
	uint8_t ppuCtrl;
	uint8_t ppuDataBuffer;
};

struct SaveState {
	bool dmaActive;
	uint8_t dmaPage;
	uint8_t dmaAddr;
	uint16_t dmaCycles;
};

class Serializer {
public:
	template<typename T>
	void Write(const T& data) {
		os->write(reinterpret_cast<const char*>(&data), sizeof(T));
	}

	template<typename T>
	void Write(const T* data, size_t size) {
		for (int i = 0; i < size; i++) {
			os->write(reinterpret_cast<const char*>(&data[i]), sizeof(T));
		}
	}

	template<typename T>
	void WriteVector(const std::vector<T>& v) {
		static_assert(std::is_trivially_copyable_v<T>,
			"Vector element type must be trivially copyable");

		uint32_t size = static_cast<uint32_t>(v.size());
		os->write(reinterpret_cast<const char*>(&size), sizeof(size));

		if (size > 0) {
			os->write(reinterpret_cast<const char*>(v.data()),
				size * sizeof(T));
		}
	}

	template<typename T, int size>
	void WriteArray(const std::array<T, size>& v) {
		static_assert(std::is_trivially_copyable_v<T>,
			"Vector element type must be trivially copyable");

		if (size > 0) {
			os->write(reinterpret_cast<const char*>(v.data()),
				size * sizeof(T));
		}
	}

	template<typename T>
	void Read(T& data) {
		is->read(reinterpret_cast<char*>(&data), sizeof(T));
	}

	template<typename T>
	void Read(T* data, size_t size) {
		for (int i = 0; i < size; i++) {
			is->read(reinterpret_cast<char*>(&data[i]), sizeof(T));
		}
	}

	template<typename T>
	void ReadVector(std::vector<T>& v) {
		static_assert(std::is_trivially_copyable_v<T>,
			"Vector element type must be trivially copyable");

		uint32_t size;
		is->read(reinterpret_cast<char*>(&size), sizeof(size));

		v.resize(size);

		if (size > 0) {
			is->read(reinterpret_cast<char*>(v.data()),
				size * sizeof(T));
		}
	}

	template<typename T, int size>
	void ReadArray(std::array<T, size>& v) {
		static_assert(std::is_trivially_copyable_v<T>,
			"Vector element type must be trivially copyable");

		if (size > 0) {
			is->read(reinterpret_cast<char*>(v.data()),
				size * sizeof(T));
		}
	}

	void StartSerialization(std::ostream& os);
	void StartDeserialization(std::istream& is);

private:
	std::istream* is;
	std::ostream* os;
};