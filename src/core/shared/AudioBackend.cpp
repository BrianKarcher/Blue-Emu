#include "AudioBackend.h"
#include <mutex>

AudioBackend::AudioBackend()
    : m_xaudio2(nullptr), m_masteringVoice(nullptr), m_sourceVoice(nullptr)
    , m_voiceCallback(this), m_sampleRate(44100), m_channels(1)
    , m_initialized(false)
    , m_ringBuffer(RING_BUFFER_CAPACITY)
{
    // Initialize chunk contexts
    for (int i = 0; i < CHUNK_COUNT; i++) {
        m_chunks[i].index = i; // Assign simple index context
    }
}

AudioBackend::~AudioBackend()
{
    Shutdown();
}

bool AudioBackend::Initialize(int sampleRate, int channels)
{
    m_sampleRate = sampleRate;
    m_channels = channels;

    // Create XAudio2 instance
    HRESULT hr = XAudio2Create(&m_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        return false;
    }

    // Create mastering voice
    hr = m_xaudio2->CreateMasteringVoice(&m_masteringVoice);
    if (FAILED(hr)) {
        m_xaudio2->Release();
        m_xaudio2 = nullptr;
        return false;
    }

    // Set up wave format
    WAVEFORMATEX waveFormat = {};
    waveFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    waveFormat.nChannels = m_channels;
    waveFormat.nSamplesPerSec = m_sampleRate;
    waveFormat.wBitsPerSample = 32; // 32-bit float
    waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
    waveFormat.cbSize = 0;

    // Create source voice
    hr = m_xaudio2->CreateSourceVoice(&m_sourceVoice, &waveFormat, 0,
        XAUDIO2_DEFAULT_FREQ_RATIO, &m_voiceCallback);
    if (FAILED(hr)) {
        m_masteringVoice->DestroyVoice();
        m_xaudio2->Release();
        m_xaudio2 = nullptr;
        return false;
    }

    // Start the voice
    m_sourceVoice->Start(0);

    m_initialized = true;
    return true;
}

void AudioBackend::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_submissionMutex);

    if (m_sourceVoice) {
        m_sourceVoice->Stop(0);
        m_sourceVoice->DestroyVoice();
        m_sourceVoice = nullptr;
    }

    if (m_masteringVoice) {
        m_masteringVoice->DestroyVoice();
        m_masteringVoice = nullptr;
    }

    if (m_xaudio2) {
        m_xaudio2->Release();
        m_xaudio2 = nullptr;
    }

    CoUninitialize();
    m_initialized = false;

    // Wake up the CV just in case it was stuck waiting for a buffer 
    // that we just flushed.
    m_audioCV.notify_all();
}

void AudioBackend::resetBuffer() {
    std::lock_guard<std::mutex> lock(m_submissionMutex);

    if (m_sourceVoice) {
        m_sourceVoice->Stop(0);            // Stop playback
        m_sourceVoice->FlushSourceBuffers(); // Clear the hardware queue
        m_sourceVoice->Start(0);           // Restart for the new run
    }

    m_ringBuffer.Reset();
    m_currentChunkIndex = 0;

    // --- PRIMING ---
    // Fill the ring buffer with silence so the core is throttled 
    // from the very first frame.
    std::vector<float> silence(SAMPLES_PER_CHUNK * (CHUNK_COUNT - 1), 0.0f);
    m_ringBuffer.Write(silence.data(), silence.size());

    // Attempt to push these to XAudio2 immediately
    TrySubmitChunk();

    // Wake up the CV just in case it was stuck waiting for a buffer 
    // that we just flushed.
    m_audioCV.notify_all();
}

void AudioBackend::SubmitSamples(const float* samples, size_t count)
{
    if (!m_initialized) return;

    // 1. Lock the mutex before doing anything
    std::unique_lock<std::mutex> lock(m_submissionMutex);

    // 2. The Blocking mechanism: 
    // If XAudio2 already has its maximum chunks queued, put the emulator thread to SLEEP.
    // This, and ONLY this, locks the game at 60 FPS.
    // Adjust as needed for faster or slower gameplay.
    m_audioCV.wait(lock, [this]() {
        XAUDIO2_VOICE_STATE state;
        m_sourceVoice->GetState(&state);
        // Continue ONLY if XAudio2 has space for more buffers
        return state.BuffersQueued < CHUNK_COUNT;
    });

    // --- Ring Buffer Write (Lock-Free) ---
    m_ringBuffer.Write(samples, count);

    // Try to submit chunks
    TrySubmitChunk();
}

void AudioBackend::TrySubmitChunk()
{
    XAUDIO2_VOICE_STATE state;
    m_sourceVoice->GetState(&state);

    // Continue as long as XAudio2 has space AND we have enough data
    while (state.BuffersQueued < CHUNK_COUNT) {

        // Use GetAvailableRead() instead of ReadPointer's contiguous count
        if (m_ringBuffer.GetAvailableRead() < SAMPLES_PER_CHUNK) {
            break;
        }

        // 1. Pull the samples out of the ring buffer into our stable chunk buffer
        m_ringBuffer.Read(m_chunks[m_currentChunkIndex].data, SAMPLES_PER_CHUNK);

        // 2. Point XAudio2 to this stable chunk buffer
        XAUDIO2_BUFFER xaudioBuffer = {};
        xaudioBuffer.AudioBytes = SAMPLES_PER_CHUNK * sizeof(float);
        xaudioBuffer.pAudioData = reinterpret_cast<const BYTE*>(m_chunks[m_currentChunkIndex].data);
        xaudioBuffer.pContext = &m_chunks[m_currentChunkIndex];
        xaudioBuffer.Flags = 0;

        HRESULT hr = m_sourceVoice->SubmitSourceBuffer(&xaudioBuffer);
        if (FAILED(hr)) break;

        m_currentChunkIndex = (m_currentChunkIndex + 1) % CHUNK_COUNT;

        // Update state for the next loop iteration
        m_sourceVoice->GetState(&state);
    }
}

void AudioBackend::VoiceCallback::OnBufferEnd(void* pBufferContext)
{
    // OnBufferEnd is called by an internal XAudio2 thread, so it must be thread-safe.
    std::lock_guard<std::mutex> lock(m_backend->m_submissionMutex);

    // NOTE: In the ring buffer model, we no longer need to free/mark specific chunks.
    // The ring buffer is always being advanced by the consumer (TrySubmitChunk).
    // The only action needed is to try and submit the next chunk.
    m_backend->TrySubmitChunk();
    // WAKE UP the emulator thread! A buffer just finished, so there is room for more.
    m_backend->m_audioCV.notify_one();
}