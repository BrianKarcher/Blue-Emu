//#include <cstdlib>
#include "pch.h"
//#include "CppUnitTest.h"
//#include "CPU.h"
//#include "Cartridge.h"
//#include "Bus.h"
//#include "nes_ppu.h"
//#include "RendererLoopy.h"
//#include "Core.h"
//
//using namespace Microsoft::VisualStudio::CppUnitTestFramework;
//
///* 
//	Tests centered around the Loopy renderer. Trust me, we need these. 
//*/
//namespace PPUTest
//{
//	TEST_CLASS(LoopyTest)
//	{
//	private:
//		//Processor_6502 processor;
//		//Bus bus;
//		//Cartridge cart;
//		//NesPPU ppu;
//		Core core;
//		RendererLoopy loopyRenderer;
//
//	public:
//		TEST_METHOD_INITIALIZE(TestSetup)
//		{
//			core.Initialize();
//			ines_file_t inesLoader;
//			core.bus.cart->SetMapper(0, inesLoader);
//			//core.cpu.PowerOn();
//			//core.cpu.Activate(true);
//			//core.cpu.SetPC(0x8000);
//			loopyRenderer.initialize(&core.ppu);
//		}
//
//		TEST_METHOD(TestGetAttributeAddress)
//		{
//			for (int nty = 0; nty < 2; nty++) {
//				for (int ntx = 0; ntx < 2; ntx++) {
//					for (int y = 0; y < 30; y++) {
//						for (int x = 0; x < 32; x++) {
//							RendererLoopy::LoopyRegister reg;
//							reg.coarse_x = x;
//							reg.coarse_y = y;
//							reg.nametable_x = ntx;
//							reg.nametable_y = nty;
//							reg.fine_y = 0;
//							uint16_t res = loopyRenderer.get_attribute_address(reg);
//							uint16_t ntaddr = (0x23C0 + (nty * 0x800) + (ntx * 0x400));
//
//							uint16_t expected = ntaddr + ((y / 4) * 8) + (x / 4);
//							Assert::AreEqual(expected, res);
//						}
//					}
//				}
//			}
//		}
//	};
//}