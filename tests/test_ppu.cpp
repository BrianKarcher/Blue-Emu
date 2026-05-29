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