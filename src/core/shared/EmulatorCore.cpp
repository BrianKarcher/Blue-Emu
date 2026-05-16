// EmulatorCore.cpp
#include "EmulatorCore.h"
#include "SharedContext.h"
#include "AudioRingBuffer.h"
#include <chrono>
#include "NesCpu.h"
#include "NesPpu.h"
#include "NesApu.h"
#include "NesBus.h"
#include "NesInput.h"
#include <vector>
#include "Serializer.h"
#include <fstream>
#include "DebuggerContext.h"
#include "RendererLoopy.h"

EmulatorCore::EmulatorCore(SharedContext& ctx) : context(ctx), nes(ctx) {
    dbgCtx = ctx.debugger_context;
    // Initialize audio backend
    if (!audioBackend.Initialize(44100, 1)) {  // 44.1kHz, mono
		throw std::runtime_error("Failed to initialize audio backend");
    }
    m_paused = true;

    // Set up DMC read callback
    nes.apu_->set_dmc_read_callback([this](uint16_t address) -> uint8_t {
        return nes.bus_->read(address);
    });

    // Get high-resolution timer frequency once
    LARGE_INTEGER freq_li;
    if (!QueryPerformanceFrequency(&freq_li)) {
        return;
    }
    freq = freq_li.QuadPart;

    // Target frame rate
    //const double targetFps = 60.0988;
    // My monitor runs at 60 FPS, any FPS above that will cause it go to out of sync.
    // TODO - Make this configurable.
    const double targetFps = 60;
    ticksPerFrame = static_cast<long long>(freq / targetFps);
}

EmulatorCore::~EmulatorCore() {
    audioBackend.Shutdown();
    nes.cart_->unload();

    nes.input_->CloseController();
}

void EmulatorCore::start() {
    core_thread = std::thread(&EmulatorCore::run, this);
}

void EmulatorCore::stop() {
    context.is_running = false;
    if (core_thread.joinable()) core_thread.join();
}

void EmulatorCore::run() {
    // Wait until a game is loaded.
    while (m_paused && context.is_running) {
        Sleep(1);
        processCommands();
    }

    LARGE_INTEGER fpsUpdateTime_li;
    QueryPerformanceCounter(&fpsUpdateTime_li);
	long long nextFpsUpdateTime = fpsUpdateTime_li.QuadPart;

    // Measure time spent so far this frame
    LARGE_INTEGER frameEnd_li;
    QueryPerformanceCounter(&frameEnd_li);
    nextFrameUpdateTime = frameEnd_li.QuadPart + ticksPerFrame;

    int frameCount = 0;
    int audioCycleCounter = 0;
    audioBackend.resetBuffer();

    while (context.is_running) {
        processCommands();
        
        if (m_paused) {
            Sleep(1);
            continue;
        }

        nes.input_->PollControllerState();
        audioCycleCounter += runFrame();
        context.SubmitFrame();
        frameCount++;

		dbgCtx->UpdateSnapshot(nes.bus_->ramMapper.cpuRAM.data(), nullptr);

        long long targetTime = nextFrameUpdateTime;
        nextFrameUpdateTime += ticksPerFrame;

        // Get current time (actual time frame execution finished)
        LARGE_INTEGER currentFrame_li;
        QueryPerformanceCounter(&currentFrame_li);
        long long currentTime = currentFrame_li.QuadPart;

        if (currentTime >= nextFpsUpdateTime) {
			// Update FPS once per second
			LOG(L"FPS: %d, cycles: %d\n", frameCount, audioCycleCounter);
            context.current_fps.store(frameCount);
            frameCount = 0;
            audioCycleCounter = 0;
            nextFpsUpdateTime += freq;
		}
	}
}

inline void EmulatorCore::processCommands() {
    CommandQueue::Command cmd;
    while (context.command_queue.TryPop(cmd)) {
        processCommand(cmd);
    }
}

inline void EmulatorCore::dbg(const wchar_t* fmt, ...) {
#ifdef EMULATORCORE_DEBUG
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
#endif
}

int EmulatorCore::runFrame() {
    // Reset frame tick from previous frame
    nes.ppu_->renderer->m_frameTick = false;
    
    // Ensure the audio buffer is clear before starting the frame
    nes.audioBuffer.clear();
	nes.cpu_->cyclesThisFrame = 0;
    nes.ppu_->setBuffer(context.GetBackBuffer());
    // Run NesPpu until frame complete (89342 cycles per frame)
	while (!nes.frameReady() && context.is_running) {
        nes.clock();
	}
	if (!context.is_running) return 0;

    // Submit the exact samples generated this frame
    // Check audio queue to prevent unbounded growth
    size_t queuedSamples = audioBackend.GetQueuedSampleCount();
    const size_t MAX_QUEUED_SAMPLES = 4410; // ~100ms of audio (44100 / 10)

    LOG(L"Cycles this frame %d, Samples this frame: %d\n", nes.cpu_->cyclesThisFrame, nes.audioBuffer.size());
    int cycleCount = nes.audioBuffer.size();
    if (!nes.audioBuffer.empty()) {
        audioBackend.SubmitSamples(nes.audioBuffer.data(), nes.audioBuffer.size());
    }

    return cycleCount;
}

void EmulatorCore::processCommand(const CommandQueue::Command& cmd) {
    switch (cmd.type) {
    case CommandQueue::CommandType::LOAD_ROM:
        audioBackend.resetBuffer();
        nes.cart_->unload();
        nes.ppu_->reset();
        nes.apu_->reset();
        nes.bus_->PowerCycle();
        nes.cart_->LoadROM(cmd.data);
        nes.cpu_->PowerCycle();
        m_paused = false;
        UpdateNextFrameTime();
        // Set up DMC read callback
        nes.apu_->set_dmc_read_callback([this](uint16_t address) -> uint8_t {
            return nes.bus_->read(address);
        });
        context.coreRunning.store(true);
        break;
    case CommandQueue::CommandType::RESET:
        audioBackend.resetBuffer();
        nes.ppu_->reset();
        nes.apu_->reset();
        nes.bus_->reset();
        nes.cpu_->Reset();
        break;
    case CommandQueue::CommandType::POWER:
        audioBackend.resetBuffer();
        nes.ppu_->reset();
        nes.apu_->reset();
        nes.bus_->PowerCycle();
        nes.cpu_->PowerCycle();
        break;
    case CommandQueue::CommandType::CLOSE:
        context.coreRunning.store(false);
        audioBackend.resetBuffer();
        nes.ppu_->reset();
        nes.apu_->reset();
        nes.bus_->PowerCycle();
        m_paused = true;
        // Set up DMC read callback
        nes.apu_->set_dmc_read_callback([this](uint16_t address) -> uint8_t {
            return nes.bus_->read(address);
        });
        break;
    case CommandQueue::CommandType::PAUSE:
        m_paused = true;
        break;
    case CommandQueue::CommandType::RESUME:
        m_paused = false;
        UpdateNextFrameTime();
        break;
    case CommandQueue::CommandType::STEP_FRAME:
        break;
    case CommandQueue::CommandType::ADD_CONTROLLER:
        nes.input_->OpenFirstController();
		break;
    case CommandQueue::CommandType::REMOVE_CONTROLLER:
        nes.input_->CloseController();
		break;
    case CommandQueue::CommandType::SAVE_STATE:
        CreateSaveState();
        break;
    case CommandQueue::CommandType::LOAD_STATE:
        LoadState();
        break;
    }
}

void EmulatorCore::UpdateNextFrameTime() {
    LARGE_INTEGER frameEnd_li;
    QueryPerformanceCounter(&frameEnd_li);
    nextFrameUpdateTime = frameEnd_li.QuadPart + ticksPerFrame;
}

void EmulatorCore::CreateSaveState() {
    std::filesystem::path appFolder = nes.cart_->getAndEnsureSavePath();
    std::filesystem::path stateFilePath = appFolder / (nes.cart_->fileName + L".000");
    std::ofstream os(stateFilePath, std::ios::binary);
    Serializer serializer;
	serializer.StartSerialization(os);
    nes.Serialize(serializer);
    if (!os) {
        LOG(L"Failed to open save state file for writing: %s\n", stateFilePath.c_str());
        return;
	}
}

void EmulatorCore::LoadState() {
    std::filesystem::path appFolder = nes.cart_->getAndEnsureSavePath();
    std::filesystem::path stateFilePath = appFolder / (nes.cart_->fileName + L".000");
    std::ifstream is(stateFilePath, std::ios::binary);
    if (!is) {
        LOG(L"Failed to open save state file for reading: %s\n", stateFilePath.c_str());
        return;
    }
    Serializer serializer;
    serializer.StartDeserialization(is);
    nes.Deserialize(serializer);
}