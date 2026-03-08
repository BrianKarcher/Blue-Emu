// AudioBackend.h
#pragma once
#include "AudioRingBuffer.h"
#include <xaudio2.h>
#include <vector>
#include <mutex>
#include <condition_variable>

#pragma comment(lib, "xaudio2.lib")

class AudioBackend {
public:
    AudioBackend();
    ~AudioBackend();

    bool Initialize(int sampleRate = 44100, int channels = 1);
    void resetBuffer();
    void Shutdown();

    // Submit audio samples (Producer side)
    void SubmitSamples(const float* samples, size_t count);

    // Get the number of queued samples
    size_t GetQueuedSampleCount() { return m_ringBuffer.GetAvailableRead(); }

    // Check if audio is initialized
    bool IsInitialized() const { return m_initialized; }

    // SAMPLES_PER_CHUNK is the size of the small XAudio2 chunks
    // One frame's worth of data. Needed for frame synchronization.
    static const int SAMPLES_PER_CHUNK = 735;
    // The total size of the circular buffer (Must be power of 2)
    static const int RING_BUFFER_CAPACITY = 8192;

private:
    std::mutex m_submissionMutex;
    std::condition_variable m_audioCV;

    struct AudioChunk {
        // This is the stable buffer XAudio2 will read from
        float data[SAMPLES_PER_CHUNK];
        int index;
    };

    // XAudio2 voice callback
    class VoiceCallback : public IXAudio2VoiceCallback {
    public:
        VoiceCallback(AudioBackend* backend) : m_backend(backend) {}

        // Called when a buffer finishes playing
        void STDMETHODCALLTYPE OnBufferEnd(void* pBufferContext) override;

        // Unused callbacks (required by interface)
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}

    private:
        AudioBackend* m_backend;
    };

    // Helper to submit the next contiguous chunk from the ring buffer
    void TrySubmitChunk();

    IXAudio2* m_xaudio2;
    IXAudio2MasteringVoice* m_masteringVoice;
    IXAudio2SourceVoice* m_sourceVoice;
    VoiceCallback m_voiceCallback;

    int m_sampleRate;
    int m_channels;
    bool m_initialized;

    // Audio buffer management
    AudioRingBuffer<float> m_ringBuffer; // The single continuous buffer

    static const int CHUNK_COUNT = 4; // Number of chunks XAudio2 will have queued
    AudioChunk m_chunks[CHUNK_COUNT];
    int m_currentChunkIndex = 0;
};