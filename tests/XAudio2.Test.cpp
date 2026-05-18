//#include <cstdlib>
//#include <iostream>
#include "pch.h"
//#include "CppUnitTest.h"
//#include "nes_apu.h"
//#include "AudioBackend.h"
//#include <cmath>
//#include <windows.h>
//#include <xaudio2.h>
//
//#pragma comment(lib, "xaudio2.lib")
//
//using namespace Microsoft::VisualStudio::CppUnitTestFramework;
//
//namespace APUTest
//{
//    TEST_CLASS(APUTest)
//    {
//        TEST_METHOD(TestXAudio2)
//        {
//            HRESULT hr;
//
//            // Initialize COM
//            //hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
//            //if (FAILED(hr)) {
//            //    //std::cerr << "Failed to initialize COM.\n";
//            //    return;
//            //}
//
//            // Create XAudio2 engine
//            IXAudio2* pXAudio2 = nullptr;
//            hr = XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
//            if (FAILED(hr)) {
//                //std::cerr << "Failed to create XAudio2 engine.\n";
//                return;
//            }
//
//            // Create mastering voice
//            IXAudio2MasteringVoice* pMasterVoice = nullptr;
//            hr = pXAudio2->CreateMasteringVoice(&pMasterVoice);
//            if (FAILED(hr)) {
//                //std::cerr << "Failed to create mastering voice.\n";
//                pXAudio2->Release();
//                return;
//            }
//
//            // Generate a 1-second 440 Hz sine wave at 44.1 kHz
//            const int sampleRate = 44100;
//            const float frequency = 440.0f;
//            const int duration = sampleRate * 1; // 1 second
//            float* waveData = new float[duration];
//            for (int i = 0; i < duration; i++) {
//                waveData[i] = 0.25f * sinf(2.0f * 3.14159265f * frequency * i / sampleRate);
//            }
//
//            // Setup wave format
//            WAVEFORMATEX waveFormat = {};
//            waveFormat.wFormatTag = WAVE_FORMAT_PCM;
//            waveFormat.nChannels = 1;
//            waveFormat.nSamplesPerSec = sampleRate;
//            waveFormat.wBitsPerSample = 16;
//            waveFormat.nBlockAlign = waveFormat.nChannels * waveFormat.wBitsPerSample / 8;
//            waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
//
//            // Convert float to 16-bit PCM
//            short* pcmData = new short[duration];
//            for (int i = 0; i < duration; i++) {
//                pcmData[i] = static_cast<short>(waveData[i] * 32767);
//            }
//
//            // Setup audio buffer
//            XAUDIO2_BUFFER buffer = {};
//            buffer.AudioBytes = duration * sizeof(short);
//            buffer.pAudioData = reinterpret_cast<BYTE*>(pcmData);
//            buffer.Flags = XAUDIO2_END_OF_STREAM;
//
//            // Create source voice
//            IXAudio2SourceVoice* pSourceVoice = nullptr;
//            hr = pXAudio2->CreateSourceVoice(&pSourceVoice, &waveFormat);
//            if (FAILED(hr)) {
//                //std::cerr << "Failed to create source voice.\n";
//                delete[] waveData;
//                delete[] pcmData;
//                pMasterVoice->DestroyVoice();
//                pXAudio2->Release();
//                return;
//            }
//
//            // Submit buffer and start playback
//            pSourceVoice->SubmitSourceBuffer(&buffer);
//            pSourceVoice->Start(0);
//
//            //std::cout << "Playing 440 Hz sine wave for 1 second...\n";
//            Sleep(1000);
//
//            // Cleanup
//            pSourceVoice->Stop(0);
//            pSourceVoice->DestroyVoice();
//            pMasterVoice->DestroyVoice();
//            pXAudio2->Release();
//            delete[] waveData;
//            delete[] pcmData;
//
//            //CoUninitialize();
//        }
//    };
//}