//#include <cstdlib>
//#include "pch.h"
//#include "CppUnitTest.h"
//#include "CPU.h"
//#include "Cartridge.h"
//#include "Bus.h"
//#include "INESLoader.h"
//#include "Nes.h"
//#include "SharedContext.h"
//#include "Mapper.h"
//#include "NROM.h"
//#include "MMC1.h"
//
//using namespace Microsoft::VisualStudio::CppUnitTestFramework;
//
//namespace BlueNESTest
//{
//	TEST_CLASS(MMC1Test)
//	{
//	private:
//		SharedContext ctx;
//		Nes* nes;
//		CPU* cpu;
//		Bus* bus;
//		Cartridge* cart;
//
//		void RunInst() {
//			bool first = true;
//			while (first || !cpu->inst_complete) {
//				first = false;
//				cpu->cpu_tick();
//			}
//		}
//
//		void WriteMMC1(uint16_t addr, uint8_t value) {
//			// Write 5 bits to the MMC1 shift register
//			for (int i = 0; i < 5; i++) {
//				uint8_t bit = (value >> i) & 1;
//				cpu->WriteByte(addr, bit); // Write bit to MMC1
//				cpu->ConsumeCycle(); // MMC1 needs a cycle to process each bit
//			}
//		}
//
//		void InitializeSUROM(std::vector<uint8_t> prgRom) {
//			ines_file_t inesHeader = {};
//			inesHeader.header.prg_rom_size = 32; // 32 x 16 KB = 512 KB
//			inesHeader.header.chr_rom_size = 0; // No CHR ROM
//			inesHeader.prg_rom = new prg_rom_data_t();
//			inesHeader.prg_rom->data = prgRom.data();
//			inesHeader.prg_rom->size = prgRom.size();
//			inesHeader.chr_rom = new chr_rom_data_t();
//			cart->mapper->initialize(inesHeader);
//			cpu->PowerCycle();
//			cpu->SetPC(0x8000);
//		}
//
//	public:
//		TEST_METHOD_INITIALIZE(TestSetup)
//		{
//			nes = new Nes(ctx);
//			cart = nes->cart_;
//			cart->mapper = new MMC1(cart, *nes->cpu_);
//			bus = nes->bus_;
//			cart->mapper->register_memory(*bus);
//			cart->mapper->m_prgRamData.resize(0x2000);
//			cpu = nes->cpu_;
//			cpu->init_cpu();
//		}
//
//		TEST_METHOD(TestSUROMBanking)
//		{
//			// Initialize 512 KB PRG ROM (32 x 16 KB banks)
//			std::vector<uint8_t> prgRom(512 * 1024, 0);
//			for (size_t i = 0; i < prgRom.size(); i++) {
//				prgRom[i] = static_cast<uint8_t>(i & 0xFF); // Fill with unique data
//			}
//			InitializeSUROM(prgRom);
//			// Set the board type to SUROM
//			auto* mmc1 = dynamic_cast<MMC1*>(cart->mapper);
//			Assert::IsNotNull(mmc1, L"Mapper is not MMC1");
//			mmc1->boardType = BoardType::SUROM;
//
//			// Test lower 256 KB (outer bank = 0)
//			WriteMMC1(0xA000, 0x00); // Set CHR bank 0 to 0x00 (outer bank = 0)
//			WriteMMC1(0xE000, 0x03); // Set PRG bank to 0x03
//			Assert::AreEqual((uint8_t)0x03, mmc1->prgBankReg, L"PRG bank not set correctly for lower 256 KB");
//			Assert::AreEqual((uint8_t)0, mmc1->suromPrgOuterBank, L"Outer bank not set to 0 for lower 256 KB");
//
//			// Verify PRG mapping
//			uint8_t prgData = bus->peek(0x8000); // Read from $8000
//			Assert::AreEqual(static_cast<uint8_t>(0x03 * 0x4000), prgData, L"PRG data incorrect for lower 256 KB");
//
//			// Test upper 256 KB (outer bank = 1)
//			WriteMMC1(0xA000, 0x10); // Set CHR bank 0 to 0x10 (outer bank = 1)
//			WriteMMC1(0xE000, 0x07); // Set PRG bank to 0x07
//			Assert::AreEqual((uint8_t)0x07, mmc1->prgBankReg, L"PRG bank not set correctly for upper 256 KB");
//			Assert::AreEqual((uint8_t)0x10, mmc1->suromPrgOuterBank, L"Outer bank not set to 1 for upper 256 KB");
//
//			// Verify PRG mapping
//			prgData = bus->peek(0x8000); // Read from $8000
//			Assert::AreEqual(static_cast<uint8_t>((0x10 + 0x07) * 0x4000), prgData, L"PRG data incorrect for upper 256 KB");
//		}
//
//		TEST_METHOD(TestSUROMBanking_3_WithSwitch)
//		{
//			// Initialize 512 KB PRG ROM (32 x 16 KB banks)
//			std::vector<uint8_t> prgRom(512 * 1024, 0);
//			for (size_t i = 0; i < prgRom.size(); i++) {
//				prgRom[i] = static_cast<uint8_t>(i / 0x4000); // Fill each 16 KB bank with its bank number
//			}
//			InitializeSUROM(prgRom);
//			// Set the board type to SUROM
//			auto* mmc1 = dynamic_cast<MMC1*>(cart->mapper);
//			Assert::IsNotNull(mmc1, L"Mapper is not MMC1");
//			mmc1->boardType = BoardType::SUROM;
//
//			// Test lower 256 KB (outer bank = 0)
//			WriteMMC1(0xA000, 0x00); // Set CHR bank 0 to 0x00 (outer bank = 0)
//			Assert::AreEqual((uint8_t)0, mmc1->suromPrgOuterBank, L"Outer bank not set to 0 for lower 256 KB");
//
//			// Verify PRG mapping
//			// Check the lower 256 KB
//			for (int j = 0; j < 15; j++) {
//				WriteMMC1(0xE000, j); // Set PRG bank to j
//				Assert::AreEqual((uint8_t)j, mmc1->prgBankReg, L"PRG bank not set correctly for lower 256 KB");
//				for (int i = 0x8000; i < 0xC000; i++) {
//					//if (i == 0xC000) { // There's an error on bank 1?
//					//	int j = 0;
//					//}
//					uint8_t prgData = bus->peek(i); // Read from $8000
//					// Expected address into the PRG ROM
//					int addr = (j * 0x4000) + (i - 0x8000);
//					std::wstring msg = L"PRG data incorrect for lower 256 KB (addr=" + std::to_wstring(addr) + L"), data=" + std::to_wstring(prgData);
//					Assert::AreEqual(static_cast<uint8_t>(addr / 0x4000), prgData, msg.c_str());
//				}
//				// Verify the fixed bank at $C000-$FFFF
//				for (int i = 0xC000; i <= 0xFFFF; i++) {
//					//if (i == 0xC000) { // There's an error on bank 1?
//					//	int j = 0;
//					//}
//					uint8_t prgData = bus->peek(i); // Read from $8000
//					std::wstring msg = L"PRG data incorrect for lower 256 KB (bank=15) data=" + std::to_wstring(prgData);
//					Assert::AreEqual(static_cast<uint8_t>(15), prgData, msg.c_str());
//				}
//			}
//
//			// Test upper 256 KB (outer bank = 1)
//			WriteMMC1(0xA000, 0x10); // Set CHR bank 0 to 0x10 (outer bank = 1)
//			Assert::AreEqual((uint8_t)0x10, mmc1->suromPrgOuterBank, L"Outer bank not set to 1 for upper 256 KB");
//
//			// Verify PRG mapping
//			// Check the upper 256 KB
//			for (int j = 0; j < 15; j++) {
//				WriteMMC1(0xE000, j); // Set PRG bank to j
//				Assert::AreEqual((uint8_t)(j), mmc1->prgBankReg, L"PRG bank not set correctly for upper 256 KB");
//				for (int i = 0x8000; i < 0xC000; i++) {
//					//if (i == 0xC000) { // There's an error on bank 1?
//					//	int j = 0;
//					//}
//					uint8_t prgData = bus->peek(i); // Read from $8000
//					// Expected address into the PRG ROM
//					int addr = (j * 0x4000) + (i - 0x8000) + (16 * 0x4000);
//					std::wstring msg = L"PRG data incorrect for lower 256 KB (addr=" + std::to_wstring(addr) + L"), data=" + std::to_wstring(prgData);
//					Assert::AreEqual(static_cast<uint8_t>(addr / 0x4000), prgData, msg.c_str());
//				}
//				// Verify the fixed bank at $C000-$FFFF
//				for (int i = 0xC000; i <= 0xFFFF; i++) {
//					//if (i == 0xC000) { // There's an error on bank 1?
//					//	int j = 0;
//					//}
//					uint8_t prgData = bus->peek(i); // Read from $8000
//					std::wstring msg = L"PRG data incorrect for upper 256 KB (bank=31) data=" + std::to_wstring(prgData);
//					Assert::AreEqual(static_cast<uint8_t>(31), prgData, msg.c_str());
//				}
//			}
//		}
//	};
//}