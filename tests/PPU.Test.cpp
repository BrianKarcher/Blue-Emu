//#include <cstdlib>
#include "pch.h"
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