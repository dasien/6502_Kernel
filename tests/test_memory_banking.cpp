/**
 * @file test_memory_banking.cpp
 * @brief Unit tests for the bankable module slot (Phase 2 infrastructure).
 *
 * The $B000-$DFFF "module window" is backed by RAM when MODULE_BANK ($FE23) is 0
 * (the boot/default state) and by a read-only ROM image for banks 1..255. These
 * tests exercise the Memory routing directly:
 *   - the bank register reads back what was written,
 *   - bank 0 leaves the window as ordinary read/write RAM,
 *   - a non-zero loaded bank reads the module image and ignores writes,
 *   - switching back to bank 0 restores the original RAM contents,
 *   - addresses just outside the window are unaffected,
 *   - an uninstalled non-zero bank reads as open bus ($00).
 */

#include <gtest/gtest.h>

#include <vector>

#include "computer/Memory.h"

using Computer::Memory;

namespace {

constexpr uint16_t kBankReg = Memory::kModuleBankRegister; // $FE23
constexpr uint16_t kWinStart = Memory::kModuleWindowStart; // $B000
constexpr uint16_t kWinEnd = Memory::kModuleWindowEnd;     // $DFFF
constexpr size_t kWinSize = Memory::kModuleWindowSize;     // 12 KB

class MemoryBankingTest : public ::testing::Test {
protected:
    // No VIC/PIA: every address under test routes to RAM or the bank logic.
    Memory mem{nullptr, nullptr};

    // A recognizable 12KB module image: byte i = (i & 0xFF) XORed with a tag so
    // it differs from any incidental RAM pattern.
    static std::vector<uint8_t> makeImage(uint8_t tag) {
        std::vector<uint8_t> img(kWinSize);
        for (size_t i = 0; i < kWinSize; ++i) {
            img[i] = static_cast<uint8_t>((i & 0xFF) ^ tag);
        }
        return img;
    }
};

TEST_F(MemoryBankingTest, ResetDefaultIsBankZero) {
    EXPECT_EQ(mem.currentBank(), 0);
    EXPECT_EQ(mem.read(kBankReg), 0);
}

TEST_F(MemoryBankingTest, BankRegisterReadsBackWrites) {
    mem.write(kBankReg, 1);
    EXPECT_EQ(mem.read(kBankReg), 1);
    EXPECT_EQ(mem.currentBank(), 1);

    mem.write(kBankReg, 255);
    EXPECT_EQ(mem.read(kBankReg), 255);

    mem.write(kBankReg, 0);
    EXPECT_EQ(mem.read(kBankReg), 0);
}

TEST_F(MemoryBankingTest, BankZeroWindowIsReadWriteRam) {
    // With bank 0, the window is ordinary RAM.
    mem.write(kWinStart, 0xAA);
    mem.write(0xC000, 0xBB);
    mem.write(kWinEnd, 0xCC);
    EXPECT_EQ(mem.read(kWinStart), 0xAA);
    EXPECT_EQ(mem.read(0xC000), 0xBB);
    EXPECT_EQ(mem.read(kWinEnd), 0xCC);
}

TEST_F(MemoryBankingTest, NonZeroBankReadsRomImage) {
    mem.loadBank(1, makeImage(0x5A));
    EXPECT_TRUE(mem.isBankLoaded(1));

    mem.write(kBankReg, 1);
    EXPECT_EQ(mem.read(kWinStart), static_cast<uint8_t>(0x00 ^ 0x5A));
    EXPECT_EQ(mem.read(kWinStart + 0x1234),
              static_cast<uint8_t>((0x1234 & 0xFF) ^ 0x5A));
    EXPECT_EQ(mem.read(kWinEnd),
              static_cast<uint8_t>(((kWinSize - 1) & 0xFF) ^ 0x5A));
}

TEST_F(MemoryBankingTest, WritesToRomBankAreIgnored) {
    mem.loadBank(1, makeImage(0x11));
    mem.write(kBankReg, 1);

    const uint8_t before = mem.read(0xC000);
    mem.write(0xC000, static_cast<uint8_t>(~before)); // attempt to overwrite ROM
    EXPECT_EQ(mem.read(0xC000), before);              // unchanged
}

TEST_F(MemoryBankingTest, SwitchingBankPreservesUnderlyingRam) {
    // Stage distinct RAM contents in the window under bank 0.
    mem.write(kWinStart, 0x42);
    mem.write(kWinEnd, 0x99);

    // Map a ROM bank: the window now shows the module, not the RAM.
    mem.loadBank(2, makeImage(0x7E));
    mem.write(kBankReg, 2);
    EXPECT_NE(mem.read(kWinStart), 0x42);

    // Back to bank 0: the original RAM is intact (bank switch is non-destructive).
    mem.write(kBankReg, 0);
    EXPECT_EQ(mem.read(kWinStart), 0x42);
    EXPECT_EQ(mem.read(kWinEnd), 0x99);
}

TEST_F(MemoryBankingTest, BanksAreIndependent) {
    mem.loadBank(1, makeImage(0x01));
    mem.loadBank(2, makeImage(0x02));

    mem.write(kBankReg, 1);
    const uint8_t b1 = mem.read(kWinStart + 0x100);
    mem.write(kBankReg, 2);
    const uint8_t b2 = mem.read(kWinStart + 0x100);

    EXPECT_EQ(b1, static_cast<uint8_t>((0x100 & 0xFF) ^ 0x01));
    EXPECT_EQ(b2, static_cast<uint8_t>((0x100 & 0xFF) ^ 0x02));
    EXPECT_NE(b1, b2);
}

TEST_F(MemoryBankingTest, AddressesOutsideWindowUnaffectedByBank) {
    mem.loadBank(1, makeImage(0x33));
    mem.write(kBankReg, 1);

    // Just below and just above the window must remain plain RAM.
    mem.write(kWinStart - 1, 0x5C); // $AFFF
    mem.write(kWinEnd + 1, 0x6D);   // $E000
    EXPECT_EQ(mem.read(kWinStart - 1), 0x5C);
    EXPECT_EQ(mem.read(kWinEnd + 1), 0x6D);
}

TEST_F(MemoryBankingTest, UninstalledBankReadsOpenBusZero) {
    EXPECT_FALSE(mem.isBankLoaded(7));
    mem.write(kBankReg, 7); // never loaded
    EXPECT_EQ(mem.read(kWinStart), 0x00);
    EXPECT_EQ(mem.read(kWinEnd), 0x00);
}

TEST_F(MemoryBankingTest, LoadBankZeroIsIgnored) {
    mem.loadBank(0, makeImage(0x44)); // bank 0 is RAM; load is a no-op
    EXPECT_FALSE(mem.isBankLoaded(0));
    // Bank 0 still behaves as RAM.
    mem.write(kWinStart, 0x77);
    EXPECT_EQ(mem.read(kWinStart), 0x77);
}

// --- Always-mapped DOS ROM ($9000-$AFFF) ----------------------------------

constexpr uint16_t kDosStart = Memory::kDosRomStart; // $9000
constexpr uint16_t kDosEnd = Memory::kDosRomEnd;     // $AFFF
constexpr size_t kDosSize = Memory::kDosRomSize;     // 8 KB

// An 8KB DOS image: byte i = (i & 0xFF) ^ tag, so it differs from RAM patterns.
static std::vector<uint8_t> makeDosImage(uint8_t tag) {
    std::vector<uint8_t> img(kDosSize);
    for (size_t i = 0; i < kDosSize; ++i) {
        img[i] = static_cast<uint8_t>((i & 0xFF) ^ tag);
    }
    return img;
}

TEST_F(MemoryBankingTest, DosRomNotLoadedRegionIsRam) {
    // The default (pre-DOS) state: $9000-$AFFF is ordinary read/write RAM.
    EXPECT_FALSE(mem.isDosRomLoaded());
    mem.write(kDosStart, 0xAA);
    mem.write(0xA000, 0xBB);
    mem.write(kDosEnd, 0xCC);
    EXPECT_EQ(mem.read(kDosStart), 0xAA);
    EXPECT_EQ(mem.read(0xA000), 0xBB);
    EXPECT_EQ(mem.read(kDosEnd), 0xCC);
}

TEST_F(MemoryBankingTest, DosRomLoadedReadsImageIgnoresWrites) {
    mem.loadDosRom(makeDosImage(0x5A));
    EXPECT_TRUE(mem.isDosRomLoaded());

    EXPECT_EQ(mem.read(kDosStart), static_cast<uint8_t>(0x00 ^ 0x5A));
    EXPECT_EQ(mem.read(kDosStart + 0x123),
              static_cast<uint8_t>((0x123 & 0xFF) ^ 0x5A));
    EXPECT_EQ(mem.read(kDosEnd),
              static_cast<uint8_t>(((kDosSize - 1) & 0xFF) ^ 0x5A));

    // Writes into the DOS ROM are ignored.
    const uint8_t before = mem.read(kDosStart);
    mem.write(kDosStart, static_cast<uint8_t>(~before));
    EXPECT_EQ(mem.read(kDosStart), before);
}

TEST_F(MemoryBankingTest, DosRomBoundariesExact) {
    mem.loadDosRom(makeDosImage(0x11));

    // Just below the DOS ROM is plain RAM; the window above (bank 0) is RAM too.
    mem.write(kDosStart - 1, 0x5C); // $8FFF
    EXPECT_EQ(mem.read(kDosStart - 1), 0x5C);
    mem.write(kDosEnd + 1, 0x6D);   // $B000 (module window, bank 0 = RAM)
    EXPECT_EQ(mem.read(kDosEnd + 1), 0x6D);

    // First and last ROM bytes come from the image (writes there are ignored).
    EXPECT_EQ(mem.read(kDosStart), static_cast<uint8_t>(0x00 ^ 0x11));
    EXPECT_EQ(mem.read(kDosEnd),
              static_cast<uint8_t>(((kDosSize - 1) & 0xFF) ^ 0x11));
}

TEST_F(MemoryBankingTest, DosRomEmptyImageLeavesRegionAsRam) {
    // Loading an empty image is a no-op: the region stays RAM.
    mem.loadDosRom(std::vector<uint8_t>{});
    EXPECT_FALSE(mem.isDosRomLoaded());
    mem.write(kDosStart, 0x42);
    EXPECT_EQ(mem.read(kDosStart), 0x42);
}

TEST_F(MemoryBankingTest, ShortImageIsZeroPadded) {
    std::vector<uint8_t> tiny{0xDE, 0xAD, 0xBE, 0xEF};
    mem.loadBank(3, tiny);
    mem.write(kBankReg, 3);
    EXPECT_EQ(mem.read(kWinStart + 0), 0xDE);
    EXPECT_EQ(mem.read(kWinStart + 3), 0xEF);
    EXPECT_EQ(mem.read(kWinStart + 4), 0x00); // padded
    EXPECT_EQ(mem.read(kWinEnd), 0x00);       // padded
}

} // namespace
