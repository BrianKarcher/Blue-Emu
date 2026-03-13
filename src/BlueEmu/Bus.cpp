#include "Bus.h"
#include "PPU.h"
#include "Cartridge.h"
#include "Input.h"
#include "NesCpu.h"
#include "APU.h"
#include "MemoryMapper.h"
#include "OpenBusMapper.h"
#include "Serializer.h"
#include <time.h>

Bus::Bus(NesCpu& cpu, PPU& ppu, APU& apu, Input& input, Cartridge& cart, OpenBusMapper& openBus)
    : cpu(cpu), ppu(ppu), apu(apu), input(input), cart(cart), openBus(openBus) {
    ramMapper.cpuRAM.fill(0);
	readMemoryMap = new MemoryMapper*[0x10000]; // 64KB address space
	writeMemoryMap = new MemoryMapper*[0x10000]; // 64KB address space
	for (int i = 0; i < 0x10000; i++) {
		readMemoryMap[i] = &openBus;
		writeMemoryMap[i] = &openBus;
	}
	srand((unsigned)time(NULL));
}

Bus::~Bus() {
	delete[] readMemoryMap;
	delete[] writeMemoryMap;
}

void Bus::reset() {
}

inline uint8_t Bus::RandomByte() {
	return rand() & 0xFF;
}

void Bus::PowerCycle() {
	// TODO - Include support for randomizing RAM on power cycle, or zero.
	ramMapper.cpuRAM.fill(0xFF);
}

void Bus::ReadRegisterAdd(uint16_t start, uint16_t end, MemoryMapper* mapper) {
	for (uint32_t addr = start; addr <= end; addr++) {
		readMemoryMap[addr] = mapper;
	}
}

void Bus::WriteRegisterAdd(uint16_t start, uint16_t end, MemoryMapper* mapper) {
    for (uint32_t addr = start; addr <= end; addr++) {
        writeMemoryMap[addr] = mapper;
    }
}

void Bus::initialize() {
	// Initialize CPU RAM mapping
	ReadRegisterAdd(0x0000, 0x1FFF, (MemoryMapper*)&ramMapper);
	WriteRegisterAdd(0x0000, 0x1FFF, (MemoryMapper*)&ramMapper);
}

uint8_t Bus::read(uint16_t addr) {
	uint8_t val = readMemoryMap[addr]->read(addr);
	openBus.setOpenBus(val);
	return val;
}

uint8_t Bus::peek(uint16_t addr) {
	return readMemoryMap[addr]->peek(addr);
}

void Bus::write(uint16_t addr, uint8_t data) {
	openBus.setOpenBus(data);
	writeMemoryMap[addr]->write(addr, data);
}

bool Bus::IrqPending() {
	return apu.get_irq_flag() || cart.mapper->IrqPending();
}

void Bus::Serialize(Serializer& serializer) {
	InternalMemoryState state;
	for (size_t i = 0; i < 2048; i++) {
		state.internalMemory[i] = ramMapper.cpuRAM[i];
	}
	serializer.Write(state);
}

void Bus::Deserialize(Serializer& serializer) {
	InternalMemoryState state;
	serializer.Read(state);
	for (size_t i = 0; i < 2048; i++) {
		ramMapper.cpuRAM[i] = state.internalMemory[i];
	}
}