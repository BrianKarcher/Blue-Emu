#pragma once
#include <Windows.h>
#include <stdint.h>
#include <array>
#include "NesCpu.h"
#include "NesCartridge.h"
#include "RAMMapper.h"
#include "NesMemoryMapper.h"

class NesPpu;
class NesApu;
class NesInput;
class OpenNesBusMapper;
class Serializer;

class NesBus
{
public:
	NesBus(NesCpu& cpu, NesPpu& ppu, NesApu& apu, NesInput& input, NesCartridge& cart, OpenNesBusMapper& openNesBus);
	~NesBus();

	RAMMapper ramMapper;
	// Some addresses are mapped to different devices, so we use a memory map
	// An example is 0x4017, which is mapped to the NesApu (write), but also to an NesInput device (read)
	NesMemoryMapper** readMemoryMap; // 64KB memory map
	NesMemoryMapper** writeMemoryMap; // 64KB memory map

	// Access functions
	void ReadRegisterAdd(uint16_t start, uint16_t end, NesMemoryMapper* mapper);
	void WriteRegisterAdd(uint16_t start, uint16_t end, NesMemoryMapper* mapper);
	uint8_t read(uint16_t addr);
	uint8_t peek(uint16_t addr);
	void write(uint16_t addr, uint8_t data);

	void initialize();

	void Serialize(Serializer& serializer);
	void Deserialize(Serializer& serializer);

	// DMA helper
	void performDMA(uint8_t page);

	void reset();
	inline uint8_t RandomByte();
	void PowerCycle();
	bool IrqPending();

	// Devices connected to the bus
	NesCpu& cpu;
	NesPpu& ppu;
	NesApu& apu;
	NesCartridge& cart;
	NesInput& input;
	OpenNesBusMapper& openNesBus;

private:

};