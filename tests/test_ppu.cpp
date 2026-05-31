#include <gtest/gtest.h>
#include <Nes.h>
#include "MapperBase.h"
#include "NesBus.h"
#include "NesCpu.h"
#include "NesCartridge.h"
#include "NesPpu.h"
#include "NROM.h"
#include "SharedContext.h"
#include "RendererLoopy.h"

namespace test_ppu
{
    class PPUEnv : public ::testing::Test {
    public:
        void SetUp() override {
            nes = new Nes(ctx);
            cart = nes->cart_;
            cart->mapper = new NROM(cart);
            uint8_t rom[0x8000];
            uint8_t chr[0x2000];
            cart->mapper->initialize(rom, sizeof(rom), chr, sizeof(chr), MapperBase::MirrorMode::HORIZONTAL);
            bus = nes->bus_;
            cart->mapper->register_memory(*bus);
            cart->mapper->m_prgRamData.resize(0x2000);
            cpu = nes->cpu_;
            cpu->init_cpu();
            cpu->PowerCycle();
            cpu->SetPC(0x8000);
			ppu = nes->ppu_;
            uint32_t buffer[256 * 240];
			ppu->setBuffer(buffer);
            // Enable rendering both bg and sprites.
			ppu->write_register(0x2001, 0b00011110);
        }
        void TearDown() override {}
        void RunInst() {
            bool first = true;
            while (first || !cpu->inst_complete) {
                first = false;
                cpu->cpu_tick();
            }
        }
        void fill_sprite(uint8_t y_pos, uint8_t tile_id, uint8_t attributes, uint8_t x_pos, std::array<uint8_t, 0x100>& oam, int sprite_index)
        {
            int base = sprite_index * 4;
            oam[base] = y_pos; // Y position
            oam[base + 1] = tile_id;  // Tile index
            oam[base + 2] = attributes;  // Attributes
            oam[base + 3] = x_pos; // X position
        }
        SharedContext ctx;
        Nes* nes;
        NesBus* bus;
        NesCpu* cpu;
        NesCartridge* cart;
		NesPpu* ppu;
    };

    TEST_F(PPUEnv, PartialSecondaryOAMClearTest)
    {
		ppu->renderer->fill_secondary_oam(0x00); // Clear secondary OAM with 0x00
		// Clear half of the secondary OAM with 0xFF to simulate sprite 0-3 being cleared, but sprite 4-7 still present
        for (int i = 0; i < 32; ++i)
        {
            ppu->Clock();
        }

		auto secondary_oam = ppu->renderer->get_secondary_oam();
		// New sprites 0-3 should be cleared to 0xFF (indicating no sprite)
		// 4 sprites, 4 bytes per sprite
        for (int i = 0; i < 4 * 4; ++i)
        {
            EXPECT_EQ(0xFF, secondary_oam[i]);
        }
		// Remaining sprites should still be cleared to 0x00
        for (int i = 5 * 4; i < 8 * 4; ++i)
        {
            EXPECT_EQ(0x00, secondary_oam[i]);
        }
    }

	// This test ensures that the secondary OAM is fully cleared to 0xFF after the first 64 dots of the scanline, even if it was previously filled with 0x00.
    TEST_F(PPUEnv, AllSecondaryOAMClearTest)
    {
        ppu->renderer->fill_secondary_oam(0x00); // Clear secondary OAM with 0x00
        for (int i = 0; i < 64; ++i)
        {
            ppu->Clock();
        }

        auto secondary_oam = ppu->renderer->get_secondary_oam();
        for (int i = 0; i < 4 * 4; ++i)
        {
            EXPECT_EQ(0xFF, secondary_oam[i]);
        }
    }

    TEST_F(PPUEnv, OAMDATAReadTest)
    {
        // OAMDATA - "Writes will increment OAMADDR after the write; reads do not."
        ppu->oam[0] = 3;
        ppu->oam[1] = 1;
        ppu->oam[2] = 2;
        for (int i = 0; i < 5; i++)
        {
            EXPECT_EQ(3, ppu->read_register(OAMDATA));
        }
    }
    
    TEST_F(PPUEnv, OAMDATAWriteTest)
    {
        // OAMDATA - "Writes will increment OAMADDR after the write; reads do not."
        ppu->oam[0] = 3;
        ppu->oam[1] = 1;
        ppu->oam[2] = 2;
        ppu->write_register(OAMDATA, 0);
        // oamaddr should be on index 1
        EXPECT_EQ(1, ppu->read_register(OAMDATA));
    }

    TEST_F(PPUEnv, SingleSpriteEvalTest)
    {
        // Fill OAM with a sprite that will be on the next scanline
        std::array<uint8_t, 0x100> oam;
		oam[0] = 0; // Y position (will be on scanline 21)
		oam[1] = 3;  // Tile index
		oam[2] = 6;  // Attributes
		oam[3] = 50; // X position
		memcpy(ppu->oam.data(), oam.data(), 0x100); // Write the sprite data to OAM
        ppu->renderer->fill_secondary_oam(0x00); // Clear secondary OAM with 0x00
		
        for (int i = 0; i < 65; ++i)
        {
            ppu->Clock();
        }
		// It should take 8 cycles to evaluate the sprite at oam[0] and copy it to secondary OAM.
        for (int i = 0; i < 8; ++i)
        {
            ppu->Clock();
        }
        auto sec_oam = ppu->renderer->get_secondary_oam();
		EXPECT_EQ(oam[0], sec_oam[0]); // Y position
		EXPECT_EQ(oam[1], sec_oam[1]); // Tile index
		EXPECT_EQ(oam[2], sec_oam[2]); // Attributes
		EXPECT_EQ(oam[3], sec_oam[3]); // X position
    }

    // Testing worst case scenario where all eight sprites are at the end of OAM.
    TEST_F(PPUEnv, EightSpritesAtEndEvalTest)
    {
        // Fill OAM with a sprites that will be on the next scanline
        std::array<uint8_t, 0x100> oam;
        oam.fill(0xFF);
        for (int i = 0; i < 8; ++i)
        {
			int sprite_index = 56 + i; // Start at index 56 to fill the last 8 sprites (56-63)
            fill_sprite(0, i, 6, 50, oam, sprite_index);
        }
		
        memcpy(ppu->oam.data(), oam.data(), 0x100); // Write the sprite data to OAM
        ppu->renderer->fill_secondary_oam(0x00); // Clear secondary OAM with 0x00

        for (int i = 0; i < 65; ++i)
        {
            ppu->Clock();
        }
		// Cycles 65 - 256 are used to evaluate the sprites and copy them to secondary OAM. Since all sprites are valid,
        // it should copy all 8 sprites without hitting the overflow evaluation phase.
        for (int i = 0; i < 192; ++i)
        {
            ppu->Clock();
        }
        auto sec_oam = ppu->renderer->get_secondary_oam();
        for (int i = 0; i < 8 * 4; ++i)
        {
            EXPECT_EQ(oam[i + 224], sec_oam[i]);
        }
    }

    // VBL is set at dot 1 of scanline 241.
    // From frame start (dot=0, scanline=0):
    //   241 scanlines * 341 dots/scanline = 82181 PPU clocks for scanlines 0-240
    //   + 1 dot for dot 1 of scanline 241
    //   = PPU clock index 82182 (0-indexed) is where VBL is set.
    //
    // In terms of CPU cycles (3 PPU clocks per CPU cycle):
    //   82182 / 3 = 27394 exactly, so this is the FIRST PPU clock of CPU cycle 27394.
    //   Because Nes::clock() runs cpu_tick() before ppu->Clock(), the CPU cannot
    //   observe VBL until cpu_tick() of CPU cycle 27395.
    static constexpr int VBL_PPU_CLOCK = 241 * 341 + 1;  // 82182
    static constexpr int VBL_CPU_CYCLE = VBL_PPU_CLOCK / 3;  // 27394 (0-indexed)

    TEST_F(PPUEnv, VblFlagSetOnCorrectPpuClock)
    {
        // The SetUp buffer is stack-allocated in SetUp() and is dangling by the time
        // any TEST_F body runs. Visible-scanline clocks write to the buffer, so
        // provide a properly scoped one.
        static uint32_t framebuf[256 * 240];
        ppu->setBuffer(framebuf);

        // Run up to (but not including) the VBL-setting clock.
        for (int i = 0; i < VBL_PPU_CLOCK; ++i)
            ppu->Clock();

        EXPECT_EQ(ppu->GetNesPpuStatus() & NesPpuSTATUS_VBLANK, 0)
            << "VBL flag must be clear before PPU clock " << VBL_PPU_CLOCK
            << " (dot 0 of scanline 241 just processed)";

        // This clock lands on dot 1 of scanline 241 — VBL must now be set.
        ppu->Clock();

        EXPECT_NE(ppu->GetNesPpuStatus() & NesPpuSTATUS_VBLANK, 0)
            << "VBL flag must be set at PPU clock " << VBL_PPU_CLOCK
            << " (dot 1 of scanline 241)";
    }

    TEST_F(PPUEnv, VblVisibleToCpuOnCorrectCycle)
    {
        // After VBL_CPU_CYCLE complete Nes::clock() iterations the PPU has processed
        // exactly 3*27394 = 82182 clocks (indices 0..82181). The VBL-setting clock
        // (index 82182) has NOT run yet, so the flag must still be clear.
        //
        // After one more iteration the PPU processes clocks 82182-82184; VBL is set
        // during clock 82182, so the flag must be set after that iteration.
        //
        // The CPU cannot read VBL during the iteration it is set (cpu_tick runs
        // before ppu->Clock() in Nes::clock()), so it first sees V=1 at CPU cycle
        // VBL_CPU_CYCLE + 1 (= 27395, 0-indexed).
        static uint32_t framebuf[256 * 240];
        ppu->setBuffer(framebuf);

        for (int i = 0; i < VBL_CPU_CYCLE; ++i)
            nes->clock();

        EXPECT_EQ(ppu->GetNesPpuStatus() & NesPpuSTATUS_VBLANK, 0)
            << "VBL must not be set after " << VBL_CPU_CYCLE << " CPU cycles";

        nes->clock();  // PPU clocks 82182-82184 run; VBL is set at 82182.

        EXPECT_NE(ppu->GetNesPpuStatus() & NesPpuSTATUS_VBLANK, 0)
            << "VBL must be set after " << (VBL_CPU_CYCLE + 1) << " CPU cycles";
    }

    // -----------------------------------------------------------------------
    // NMI timing tests
    //
    // NMI fires when VBL is set AND PPUCTRL bit 7 (NMI_ENABLE) is set.
    // In the CPU-first loop (cpu_tick runs before ppu->Clock()):
    //
    //   Nes::clock() iteration VBL_CPU_CYCLE (= 27394, 0-indexed):
    //     cpu_tick #27394  – NMI not yet available (PPU hasn't run yet)
    //     ppu.Clock() #82182 – VBL set; setNMI(true) called
    //                          nmi_line = true, nmi_need = true  (fixed timing)
    //
    //   Nes::clock() iteration VBL_CPU_CYCLE+1:
    //     cpu_tick #27395  – nmi_previous_need = nmi_need = true (at END)
    //
    //   Nes::clock() iteration VBL_CPU_CYCLE+2:
    //     cpu_tick #27396  – inst_complete=true (27396 is even with all-NOP ROM),
    //                        nmi_previous_need=true → NMI handler begins (T0)
    //
    //   NMI handler T0–T6 spans cpu_ticks 27396–27402.
    //   T6 loads PC = NMI vector ($C000 in the test setup).
    //
    //   cpu_tick #27403 fetches the first instruction (RTI, $40) at $C000,
    //   advancing PC to $C001.
    //
    //   Total from frame start: VBL_CPU_CYCLE + 9 iterations complete, then
    //   one more lands on cpu_tick #27403 → PC = $C001.
    //   So run VBL_CPU_CYCLE + 10 iterations total.
    // -----------------------------------------------------------------------

    TEST_F(PPUEnv, NmiLineGoesHighAtVbl)
    {
        static uint32_t framebuf[256 * 240];
        ppu->setBuffer(framebuf);
        ppu->write_register(0x2000, 0x80);  // Enable NMI (PPUCTRL bit 7)

        // VBL has not fired yet after VBL_CPU_CYCLE complete iterations.
        for (int i = 0; i < VBL_CPU_CYCLE; ++i)
            nes->clock();

        EXPECT_FALSE(cpu->IsNmiLineHigh())
            << "NMI line must be low before the VBL-setting iteration";

        // One more iteration: ppu.Clock() #82182 fires VBL and asserts NMI.
        nes->clock();

        EXPECT_TRUE(cpu->IsNmiLineHigh())
            << "NMI line must go high in the same iteration VBL is set";
    }

    TEST_F(PPUEnv, NmiNotFiredWithoutEnable)
    {
        static uint32_t framebuf[256 * 240];
        ppu->setBuffer(framebuf);
        // PPUCTRL left at 0 (NMI_ENABLE bit clear – the fixture default).

        for (int i = 0; i <= VBL_CPU_CYCLE; ++i)
            nes->clock();

        // VBL flag in PPUSTATUS must be set regardless of NMI_ENABLE.
        EXPECT_NE(ppu->GetNesPpuStatus() & NesPpuSTATUS_VBLANK, 0)
            << "VBL flag should be set even when NMI is disabled";

        // But the NMI line must remain low.
        EXPECT_FALSE(cpu->IsNmiLineHigh())
            << "NMI line must stay low when PPUCTRL NMI_ENABLE (bit 7) is clear";
    }

    TEST_F(PPUEnv, NmiHandlerReachesVectorWithinExpectedCycles)
    {
        // Fill $8000-$BFFF with NOPs so the CPU is always at a 2-cycle instruction
        // boundary, making inst_complete parity deterministic.
        memset(cart->mapper->m_prgRomData, 0xEA, 0x4000);

        // NMI vector → $C000.  RTI ($40) at $C000 keeps the handler self-contained.
        cart->mapper->m_prgRomData[0x4000] = 0x40;       // RTI at $C000
        cart->mapper->m_prgRomData[0x7FFA] = 0x00;       // NMI vector low  ($FFFA)
        cart->mapper->m_prgRomData[0x7FFB] = 0xC0;       // NMI vector high ($FFFB) → $C000

        static uint32_t framebuf[256 * 240];
        ppu->setBuffer(framebuf);
        ppu->write_register(0x2000, 0x80);  // Enable NMI

        // Run exactly VBL_CPU_CYCLE + 10 iterations:
        //   +1  – VBL fires (ppu.Clock #82182 asserts NMI)
        //   +1  – cpu_tick #27395 latches nmi_previous_need = true
        //   +1  – cpu_tick #27396 takes NMI (T0); NMI handler begins
        //   +6  – NMI handler T1–T6; T6 loads PC = $C000
        //   +1  – cpu_tick #27403 fetches RTI at $C000; PC advances to $C001
        for (int i = 0; i < VBL_CPU_CYCLE + 10; ++i)
            nes->clock();

        EXPECT_EQ(cpu->GetPC(), 0xC001)
            << "CPU should be executing the NMI handler (RTI at $C000 just fetched, "
               "PC = $C001) after exactly VBL_CPU_CYCLE + 10 iterations";
    }

    int main(int argc, char** argv)
    {
        ::testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
    }

}

//#include <cstdlib>
//#include "pch.h"
//#include "CppUnitTest.h"
//#include "CPU.h"
//#include "Cartridge.h"
//#include "Bus.h"
//#include "PPU.h"
//#include "RendererWithReg.h"
//#include "Nes.h"
//#include "SharedContext.h"
//
//using namespace Microsoft::VisualStudio::CppUnitTestFramework;
//
//namespace PPUTest
//{
//	TEST_CLASS(PPUTest)
//	{
//	private:
//		//Processor_6502 processor;
//		//Bus bus;
//		//Cartridge cart;
//		//NesPPU ppu;
//		SharedContext context;
//		//Nes nes(SharedContext);
//		Nes* nes;
//
//	public:
//		TEST_METHOD_INITIALIZE(TestSetup)
//		{
//			nes = new Nes(context);
//			//core.Initialize();
//			ines_file_t inesLoader;
//			nes->bus.cart.SetMapper(0, inesLoader);
//			nes->cpu.PowerOn();
//			nes->cpu.Activate(true);
//			nes->cpu.SetPC(0x8000);
//		}
//
//		TEST_METHOD(TestReadWritePPUCTRL)
//		{
//			nes->ppu.write_register(0x2000, 0x80);
//			Assert::AreEqual((uint8_t)0x80, nes->ppu.read_register(0x2000));
//		}
//
//		TEST_METHOD(TestWritePPUADDR)
//		{
//			nes->ppu.write_register(PPUADDR, 0x20);
//			// The first write sets the high byte of the TEMP address
//			// But does not update the actual VRAM address yet
//			Assert::AreEqual((uint16_t)0x0000, nes->ppu.GetVRAMAddress());
//			nes->ppu.write_register(PPUADDR, 0x00);
//			// The second write sets the low byte and updates the VRAM address
//			Assert::AreEqual((uint16_t)0x2000, nes->ppu.GetVRAMAddress());
//		}
//		TEST_METHOD(TestWritePPUSCROLL)
//		{
//			core.ppu.write_register(PPUSCROLL, 0x05);
//			core.ppu.write_register(PPUSCROLL, 0x06);
//			auto x = core.ppu.GetScrollX();
//			auto y = core.ppu.GetScrollY();
//			Assert::AreEqual((uint8_t)0x05, x);
//			Assert::AreEqual((uint8_t)0x06, y);
//		}
//		TEST_METHOD(TestWritePPUDATA)
//		{
//			core.ppu.SetVRAMAddress(0x2000);
//			core.ppu.write_register(PPUDATA, 0x55);
//			Assert::AreEqual((uint16_t) 0x2001, core.ppu.GetVRAMAddress());
//			Assert::AreEqual((uint8_t)0x55, core.ppu.ReadVRAM(0x2000));
//		}
//		TEST_METHOD(TestWritePPUDATAVert)
//		{
//			core.ppu.write_register(PPUCTRL, 0x04); // Set vertical increment
//			core.ppu.SetVRAMAddress(0x2000);
//			core.ppu.write_register(PPUDATA, 0x55);
//			Assert::AreEqual((uint16_t)0x2020, core.ppu.GetVRAMAddress());
//			Assert::AreEqual((uint8_t)0x55, core.ppu.ReadVRAM(0x2000));
//		}
//
//		TEST_METHOD(TestReadPPUDATA)
//		{
//			core.ppu.SetVRAMAddress(0x2000);
//			core.ppu.write_register(PPUDATA, 0x55);
//			core.ppu.SetVRAMAddress(0x2000);
//			// First read returns the buffered value (initially 0)
//			Assert::AreEqual((uint8_t)0x00, core.ppu.read_register(PPUDATA));
//			// Second read returns the actual value
//			Assert::AreEqual((uint8_t)0x55, core.ppu.read_register(PPUDATA));
//			Assert::AreEqual((uint16_t)0x2002, core.ppu.GetVRAMAddress());
//		}
//		TEST_METHOD(TestReadPPUDATAx2)
//		{
//			core.ppu.SetVRAMAddress(0x2000);
//			core.ppu.write_register(PPUDATA, 0x55);
//			core.ppu.write_register(PPUDATA, 0x66);
//			core.ppu.SetVRAMAddress(0x2000);
//			// First read returns the buffered value (initially 0)
//			Assert::AreEqual((uint8_t)0x00, core.ppu.read_register(PPUDATA));
//			// Second read returns the actual value
//			Assert::AreEqual((uint8_t)0x55, core.ppu.read_register(PPUDATA));
//			Assert::AreEqual((uint8_t)0x66, core.ppu.read_register(PPUDATA));
//			Assert::AreEqual((uint16_t)0x2003, core.ppu.GetVRAMAddress());
//		}
//
//		TEST_METHOD(TestReadPPUDATAVertx2)
//		{
//			core.ppu.write_register(PPUCTRL, 0x04); // Set vertical increment
//			core.ppu.SetVRAMAddress(0x2000);
//			core.ppu.write_register(PPUDATA, 0x55);
//			core.ppu.write_register(PPUDATA, 0x66);
//			core.ppu.SetVRAMAddress(0x2000);
//			// First read returns the buffered value (initially 0)
//			Assert::AreEqual((uint8_t)0x00, core.ppu.read_register(PPUDATA));
//			// Second read returns the actual value
//			Assert::AreEqual((uint8_t)0x55, core.ppu.read_register(PPUDATA));
//			Assert::AreEqual((uint8_t)0x66, core.ppu.read_register(PPUDATA));
//			Assert::AreEqual((uint16_t)0x2060, core.ppu.GetVRAMAddress());
//		}
//
//		TEST_METHOD(TestWriteScrollThenAddr) {
//			// A more complicated series of commands taken from a game log.
//			// Ensure palette data is writing correctly after fudging with it a bit.
//			core.ppu.write_register(PPUSCROLL, 0x00);
//			core.ppu.write_register(PPUSCROLL, 0x00);
//			core.ppu.read_register(PPUSTATUS);
//			core.ppu.write_register(PPUADDR, 0x3F);
//			core.ppu.write_register(PPUADDR, 0x00);
//			core.ppu.write_register(PPUCTRL, 0x30);
//			core.ppu.write_register(PPUDATA, 0x36);
//			core.ppu.write_register(PPUDATA, 0x0F);
//
//			core.ppu.read_register(PPUSTATUS);
//			core.ppu.write_register(PPUADDR, 0x3F);
//			core.ppu.write_register(PPUADDR, 0x00);
//			Assert::AreEqual((uint8_t)0x36, core.ppu.read_register(PPUDATA));
//			Assert::AreEqual((uint8_t)0x0F, core.ppu.read_register(PPUDATA));
//		}
//	};
//}