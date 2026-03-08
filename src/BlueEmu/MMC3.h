#pragma once
#include <stdint.h>
#include <array>
#include "MapperBase.h"
#include "A12Mapper.h"

class Bus;
class Cartridge;
class CPU;
class RendererLoopy;

#ifdef _DEBUG
#define LOG(...) dbg(__VA_ARGS__)
#else
#define LOG(...) do {} while(0) // completely removed by compiler
#endif

//#define MMC3DEBUG

class MMC3 : public MapperBase, public A12Mapper
{
public:
	MMC3(Bus& bus, uint8_t prgRomSize, uint8_t chrRomSize);
	~MMC3();

	void initialize(ines_file_t& data) override;
	void shutdown();

	void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle);
	void ClockIRQCounter(uint16_t ppu_address);
	bool IrqPending();
	void RecomputePrgMappings() override;
	void RecomputeChrMappings() override;
	void Serialize(Serializer& serializer) override;
	void Deserialize(Serializer& serializer) override;

private:
	inline void dbg(const wchar_t* fmt, ...);
	void triggerIRQ();
	void acknowledgeIRQ();
	RendererLoopy* renderLoopy;
	uint8_t prgMode;
	uint8_t chrMode;
	uint8_t banks[8] = { 0 };          // raw bank numbers (after masking)
	uint8_t prgBank16kCount;
	uint8_t prgBank8kCount;
	uint8_t chrBank8kCount;
	uint8_t chrBank1kCount;
	bool isFourScreen = false;

	uint8_t m_regSelect;

	Cartridge* cart;
	CPU& cpu;
	Bus& bus;

	// IRQ state
	uint8_t irq_latch;
	uint8_t irq_counter;
	bool irq_reload;
	bool irq_enabled;
	bool _irqPending;

	// A12 tracking
	bool last_a12;
	long a12LowCycle = 0;  // CPU cycle at the start of the last low time sequence
};