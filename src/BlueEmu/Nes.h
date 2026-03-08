// Nes.h

#pragma once
#include <cstdint>
#include <vector>
#include <string>

const double CPU_FREQ = 1789773.0;
const double CYCLES_PER_SAMPLE = CPU_FREQ / 44100.0;  // 40.58 exact
const int TARGET_SAMPLES_PER_FRAME = 735; // 44100 / 60 = 735 samples per frame

class Bus;
class PPU;
class Cartridge;
class CPU;
class APU;
class Input;
class SharedContext;
class AudioMapper;
class ReadController1Mapper;
class ReadController2Mapper;
class OpenBusMapper;
class Serializer;
class DebuggerContext;

class Nes
{
public:
	Bus& bus() { return *bus_; }
	PPU& ppu() { return *ppu_; }
	Cartridge& cart() { return *cart_; }
	CPU& cpu() { return *cpu_; }
	APU& apu() { return *apu_; }
	Input& input() { return *input_; }
	SharedContext& context() { return *context_; }
	AudioMapper& audioMapper() {
		return *audioMapper_;
	}

	Nes(SharedContext& ctx);
	~Nes();

	bool loadRom(const std::wstring& filepath);
	void reset();
	void clock();
	bool frameReady();

	// OAM DMA
	bool dmaActive;
	uint8_t dmaPage;
	uint8_t dmaAddr;
	uint16_t dmaCycles;

	// Audio buffer for queueing samples
	std::vector<float> audioBuffer;

	Bus* bus_;
	PPU* ppu_;
	Cartridge* cart_;
	CPU* cpu_;
	APU* apu_;
	Input* input_;
	SharedContext* context_;
	AudioMapper* audioMapper_;
	ReadController1Mapper* readController1Mapper_;
	ReadController2Mapper* readController2Mapper_;
	OpenBusMapper* openBus_;
	DebuggerContext* _debuggerContext;

	void Serialize(Serializer& serializer);
	void Deserialize(Serializer& serializer);

private:

	double audioFraction = 0.0;  // Per-frame fractional pos
};