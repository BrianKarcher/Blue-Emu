#pragma once
#include <Windows.h>
#include <stdint.h>
#include <array>
#include "NesCpu.h"
#include "Cartridge.h"
#include "RAMMapper.h"
#include "MemoryMapper.h"

class NesPpu;
class NesApu;
class Input;
class OpenNesBusMapper;
class Serializer;

class NesBus
{
public:
	NesBus(NesCpu& cpu, NesPpu& ppu, NesApu& apu, Input& input, Cartridge& cart, OpenNesBusMapper& openNesBus);
	~NesBus();

	RAMMapper ramMapper;
	// Some addresses are mapped to different devices, so we use a memory map
	// An example is 0x4017, which is mapped to the NesApu (write), but also to an Input device (read)
	MemoryMapper** readMemoryMap; // 64KB memory map
	MemoryMapper** writeMemoryMap; // 64KB memory map

	// Access functions
	void ReadRegisterAdd(uint16_t start, uint16_t end, MemoryMapper* mapper);
	void WriteRegisterAdd(uint16_t start, uint16_t end, MemoryMapper* mapper);
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
	Cartridge& cart;
	Input& input;
	OpenNesBusMapper& openNesBus;

private:

};