#include "Nes.h"
#include "Bus.h"
#include "PPU.h"
#include "Cartridge.h"
#include "CPU.h"
#include "APU.h"
#include "Input.h"
#include "SharedContext.h"
#include <vector>
#include "AudioMapper.h"
#include "InputMappers.h"
#include "OpenBusMapper.h"
#include "Serializer.h"
#include "DebuggerContext.h"

#define PPU_CYCLES_PER_CPU_CYCLE 3

Nes::Nes(SharedContext& ctx) {
    context_ = &ctx;
	apu_ = new APU();
    input_ = new Input();
    openBus_ = new OpenBusMapper();
    _debuggerContext = ctx.debugger_context;
    ppu_ = new PPU(ctx, *this);
    cpu_ = new CPU(*openBus_, ctx, *_debuggerContext, *ppu_);
    cart_ = new Cartridge(ctx, *cpu_);
    bus_ = new Bus(*cpu_, *ppu_, *apu_, *input_, *cart_, *openBus_);
    bus_->initialize();
	cpu_->connectBus(bus_);
	ppu_->connectBus(bus_);
	cart_->connectBus(bus_);
    ppu_->initialize();
	ppu_->register_memory(*bus_);
	audioMapper_ = new AudioMapper(*apu_);
    audioMapper_->register_memory(*bus_);
    readController1Mapper_ = new ReadController1Mapper(*input_);
    readController1Mapper_->register_memory(*bus_);
    readController2Mapper_ = new ReadController2Mapper(*input_);
    readController2Mapper_->register_memory(*bus_);
    audioBuffer.reserve(4096);
    dmaActive = false;
}

Nes::~Nes() {
    if (bus_) {
        delete bus_;
		bus_ = nullptr;
    }
    if (cpu_) {
        delete cpu_;
		cpu_ = nullptr;
    }
    if (ppu_) {
		delete ppu_;
        ppu_ = nullptr;
    }
    if (cart_) {
        delete cart_;
		cart_ = nullptr;
    }
}

/// <summary>
/// Performs a single clock cycle for the NES, handling DMA if active.
/// </summary>
void Nes::clock() {
    if (dmaActive) {
        // CPU stalled
        dmaCycles--;

        // Perform one byte every 2 cycles
        if ((dmaCycles & 1) == 0) {
            uint8_t val = bus_->read((dmaPage << 8) | dmaAddr);
            ppu_->writeOAM(dmaAddr, val);
            dmaAddr++;
        }
        bool irq_active = apu_->get_irq_flag() || cart_->mapper->IrqPending();
        cpu_->setIRQ(irq_active);
        cpu_->ConsumeCycle();
        ppu_->Clock();
        ppu_->Clock();
        ppu_->Clock();
        apu_->step();

        // Generate audio sample based on cycle timing
        audioFraction += 1.0;
        while (audioFraction >= CYCLES_PER_SAMPLE) {
            audioBuffer.push_back(apu_->get_output());
            audioFraction -= CYCLES_PER_SAMPLE;
        }

        if (dmaCycles == 0) {
            dmaActive = false;
        }
    }
    else {
        bool apu_irq = apu_->get_irq_flag();
        bool mapper_irq = cart_->mapper->IrqPending();
        bool irq_active = apu_irq || mapper_irq;
        cpu_->setIRQ(irq_active);
        cpu_->cpu_tick();

        ppu_->Clock();
        ppu_->Clock();
        ppu_->Clock();
        apu_->step();

        // Generate audio sample based on cycle timing
        audioFraction += 1.0;
        while (audioFraction >= CYCLES_PER_SAMPLE) {
            audioBuffer.push_back(apu_->get_output());
            audioFraction -= CYCLES_PER_SAMPLE;
        }
    }
}

bool Nes::frameReady() {
	return ppu_->isFrameTicked();
}

void Nes::Serialize(Serializer& serializer) {
    cpu_->Serialize(serializer);
	ppu_->Serialize(serializer);
	bus_->Serialize(serializer);
    SaveState data;
    data.dmaActive = dmaActive;
    data.dmaPage = dmaPage;
    data.dmaAddr = dmaAddr;
    data.dmaCycles = dmaCycles;
	serializer.Write(data);
	cart_->mapper->Serialize(serializer);
	apu_->Serialize(serializer);
}

void Nes::Deserialize(Serializer& serializer) {
    cpu_->Deserialize(serializer);
	ppu_->Deserialize(serializer);
	bus_->Deserialize(serializer);
    SaveState data;
	serializer.Read(data);
    dmaActive = data.dmaActive;
    dmaPage = data.dmaPage;
    dmaAddr = data.dmaAddr;
    dmaCycles = data.dmaCycles;
	cart_->mapper->Deserialize(serializer);
	apu_->Deserialize(serializer);
}