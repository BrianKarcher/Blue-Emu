#include "NesBus.h"
#include "NesPpu.h"
#include "NesCartridge.h"
#include "NesInput.h"
#include "NesCpu.h"
#include "NesApu.h"
#include "NesMemoryMapper.h"
#include "OpenNesBusMapper.h"
#include "Serializer.h"
#include <time.h>

NesBus::NesBus(NesCpu& cpu, NesPpu& ppu, NesApu& apu, NesInput& input, NesCartridge& cart, OpenNesBusMapper& openNesBus)
    : cpu(cpu), ppu(ppu), apu(apu), input(input), cart(cart), openNesBus(openNesBus) {
    ramMapper.cpuRAM.fill(0);
	readMemoryMap = new NesMemoryMapper*[0x10000]; // 64KB address space
	writeMemoryMap = new NesMemoryMapper*[0x10000]; // 64KB address space
	for (int i = 0; i < 0x10000; i++) {
		readMemoryMap[i] = &openNesBus;
		writeMemoryMap[i] = &openNesBus;
	}
	srand((unsigned)time(NULL));
}

NesBus::~NesBus() {
	delete[] readMemoryMap;
	delete[] writeMemoryMap;
}

void NesBus::reset() {
}

inline uint8_t NesBus::RandomByte() {
	return rand() & 0xFF;
}

void NesBus::PowerCycle() {
	// TODO - Include support for randomizing RAM on power cycle, or zero.
	ramMapper.cpuRAM.fill(0xFF);
}

void NesBus::ReadRegisterAdd(uint16_t start, uint16_t end, NesMemoryMapper* mapper) {
	for (uint32_t addr = start; addr <= end; addr++) {
		readMemoryMap[addr] = mapper;
	}
}

void NesBus::WriteRegisterAdd(uint16_t start, uint16_t end, NesMemoryMapper* mapper) {
    for (uint32_t addr = start; addr <= end; addr++) {
        writeMemoryMap[addr] = mapper;
    }
}

void NesBus::initialize() {
	// Initialize CPU RAM mapping
	ReadRegisterAdd(0x0000, 0x1FFF, (NesMemoryMapper*)&ramMapper);
	WriteRegisterAdd(0x0000, 0x1FFF, (NesMemoryMapper*)&ramMapper);
}

uint8_t NesBus::read(uint16_t addr) {
	uint8_t val = readMemoryMap[addr]->read(addr);
	openNesBus.setOpenNesBus(val);
	return val;
}

uint8_t NesBus::peek(uint16_t addr) {
	return readMemoryMap[addr]->peek(addr);
}

void NesBus::write(uint16_t addr, uint8_t data) {
	openNesBus.setOpenNesBus(data);
	writeMemoryMap[addr]->write(addr, data);
}

bool NesBus::IrqPending() {
	return apu.get_irq_flag() || cart.mapper->IrqPending();
}

void NesBus::Serialize(Serializer& serializer) {
	InternalMemoryState state;
	for (size_t i = 0; i < 2048; i++) {
		state.internalMemory[i] = ramMapper.cpuRAM[i];
	}
	serializer.Write(state);
}

void NesBus::Deserialize(Serializer& serializer) {
	InternalMemoryState state;
	serializer.Read(state);
	for (size_t i = 0; i < 2048; i++) {
		ramMapper.cpuRAM[i] = state.internalMemory[i];
	}
}