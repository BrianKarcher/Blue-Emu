//#include <cstdlib>
//#include "pch.h"
//#include "CppUnitTest.h"
//#include "APU.h"
//#include "AudioBackend.h"
//
//using namespace Microsoft::VisualStudio::CppUnitTestFramework;
//
//namespace APUTest
//{
//    TEST_CLASS(APUTest)
//    {
//    private:
//        APU apu;
//        AudioBackend audioBackend;
//
//    public:
//        TEST_METHOD_INITIALIZE(TestSetup)
//        {
//            Assert::IsTrue(audioBackend.Initialize(44100, 1), L"Failed to initialize audio backend");
//        }
//
//        TEST_METHOD_CLEANUP(TestCleanup)
//        {
//            audioBackend.Shutdown();
//        }
//
//        TEST_METHOD(TestAPUTone)
//        {
//            APU apu;
//
//            // Setup pulse channel 1 for 440 Hz tone (with halt to prevent decay)
//            const uint16_t timer = 253;  // ~440 Hz: CPU / (16 * (timer + 1))
//            apu.write_register(0x4015, 0x01);          // Enable pulse 1 FIRST
//            apu.write_register(0x4000, 0xBF);          // Duty 50% (0x80), halt (0x20), constant vol 15 (0x1F)
//            apu.write_register(0x4001, 0x00);          // Sweep disabled
//            apu.write_register(0x4002, timer & 0xFF);  // Timer low (0xFD)
//            apu.write_register(0x4003, 0x08);          // Timer high (0) + max length index 1 (254 units)
//
//            // NES timing constants
//            const float CPU_FREQ = 1789773.0f;
//            const float SAMPLE_RATE = 44100.0f;
//            const float CPU_PER_SAMPLE = CPU_FREQ / SAMPLE_RATE;
//
//            std::vector<float> buffer;
//            buffer.reserve(735);  // ~1 frame (44100 / 60)
//
//            float cpu_accum = 0.0f;
//            static const int TEST_FRAMES = 180;  // ~3 seconds
//
//            Logger::WriteMessage("Playing 440 Hz tone...\n");
//
//            for (int f = 0; f < TEST_FRAMES; f++)
//            {
//                int samples_per_frame = static_cast<int>(SAMPLE_RATE / 60.0f);
//                for (int s = 0; s < samples_per_frame; s++)
//                {
//                    cpu_accum += CPU_PER_SAMPLE;
//                    while (cpu_accum >= 1.0f)
//                    {
//                        apu.step();
//                        cpu_accum -= 1.0f;
//                    }
//
//                    float sample = apu.get_output();
//                    buffer.push_back(sample);
//                }
//
//                audioBackend.SubmitSamples(buffer.data(), buffer.size());
//                buffer.clear();
//            }
//
//            // Wait for playback to finish (critical to hear full tone)
//            auto start = std::chrono::steady_clock::now();
//            while (audioBackend.GetQueuedSampleCount() > 0)
//            {
//                std::this_thread::sleep_for(std::chrono::milliseconds(10));
//                if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() > 5)
//                {
//                    Assert::Fail(L"Timeout waiting for audio to play");
//                }
//            }
//
//            Logger::WriteMessage("Tone finished.\n");
//        }
//
//        TEST_METHOD(TestTriangle_440Hz)
//        {
//            APU apu;
//
//            // === Setup Triangle Channel for ~440 Hz ===
//            // freq = CPU / (32 * (timer + 1))
//            // 440 = 1789773 / (32 * (timer + 1)) -> timer = 126
//            const uint16_t timer = 126;
//
//            apu.write_register(0x4015, 0x04);                    // Enable triangle
//            apu.write_register(0x4008, 0x80 | 0x40);             // Control: halt length, reload = 64
//            apu.write_register(0x400A, timer & 0xFF);            // Timer low
//            apu.write_register(0x400B, (timer >> 8) | 0xF8);     // Timer high + max length (31)
//
//            // === Timing ===
//            const float CPU_FREQ = 1789773.0f;
//            const float SAMPLE_RATE = 44100.0f;
//            const float CPU_PER_SAMPLE = CPU_FREQ / SAMPLE_RATE;
//
//            std::vector<float> buffer;
//            buffer.reserve(735);
//
//            float cpu_cycles = 0.0f;
//            const int FRAMES = 240;  // ~4 seconds
//
//            Logger::WriteMessage("Playing 440 Hz triangle wave...\n");
//
//            for (int f = 0; f < FRAMES; ++f)
//            {
//                int samples_per_frame = static_cast<int>(SAMPLE_RATE / 60.0f);
//                for (int s = 0; s < samples_per_frame; ++s)
//                {
//                    cpu_cycles += CPU_PER_SAMPLE;
//                    while (cpu_cycles >= 1.0f)
//                    {
//                        apu.step();
//                        cpu_cycles -= 1.0f;
//                    }
//                    buffer.push_back(apu.get_output());
//                }
//                audioBackend.SubmitSamples(buffer.data(), buffer.size());
//                buffer.clear();
//            }
//
//            // Wait for playback
//            auto start = std::chrono::steady_clock::now();
//            while (audioBackend.GetQueuedSampleCount() > 0)
//            {
//                std::this_thread::sleep_for(std::chrono::milliseconds(10));
//                if (std::chrono::duration_cast<std::chrono::seconds>(
//                    std::chrono::steady_clock::now() - start).count() > 6)
//                {
//                    Assert::Fail(L"Audio timeout");
//                }
//            }
//
//            Logger::WriteMessage("Triangle wave played.\n");
//        }
//    };
//}
