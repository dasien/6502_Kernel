/**
 * @file test_dos_blockio.cpp
 * @brief Exercises the MFC-DOS block-device primitives (phase 2, step 2.2).
 *
 * The DOS ROM ($9000-$AFFF) exposes 6502 routines that move whole 512-byte
 * sectors between a host disk.img and a RAM buffer, driving the $FE24-$FE28
 * registers. They are reached through the DOS ABI jump table at $AF00:
 *
 *   $AF15  BLK_READ_SECTOR   read sector A/X  -> RAM buffer at (BLK_BUF_PTR)
 *   $AF18  BLK_WRITE_SECTOR  write RAM buffer -> sector A/X
 *
 * with A = LBA low, X = LBA high, and BLK_BUF_PTR ($3A/$3B) pointing at the
 * caller's 512-byte buffer. These tests load the real dos.rom, point the block
 * device at a temp image, then actually run the 6502 routines and check the
 * transfer end-to-end against the C++ block device.
 */

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "computer/BlockDevice.h"
#include "computer/Computer6502.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"

using Computer::BlockDevice;
using Computer::Computer6502;
using Computer::CPU6502;
using Computer::Memory;

namespace {

constexpr size_t kSectorSize = BlockDevice::kSectorSize; // 512

// DOS ABI jump-table entry points (fixed addresses; see dos.asm).
constexpr uint16_t kBlkReadEntry = 0xAF15;
constexpr uint16_t kBlkWriteEntry = 0xAF18;

// DOS zero-page sector-buffer pointer.
constexpr uint16_t kBlkBufPtr = 0x003A;

// A RAM buffer the routines read/write through (well clear of ROMs and the
// monitor/BASIC zero page).
constexpr uint16_t kRamBuf = 0x0800;

std::array<uint8_t, kSectorSize> makeSector(uint8_t seed) {
    std::array<uint8_t, kSectorSize> s{};
    for (size_t i = 0; i < kSectorSize; ++i) {
        s[i] = static_cast<uint8_t>((i * 3 + seed * 29) & 0xFF);
    }
    return s;
}

class DosBlockIoTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        image_path_ = (std::filesystem::temp_directory_path() /
                       ("mfcdos_blockio_test_" + std::to_string(++counter) + ".img"))
                          .string();
        std::error_code ec;
        std::filesystem::remove(image_path_, ec);

        computer.power_on(); // loads kernel/basic/devtools/dos ROMs
        mem_ = computer.getMemory();
        cpu_ = computer.getCpu();
        computer.getBlockDevice()->setImagePath(image_path_);

        // Sanity: the DOS ROM is actually mapped (signature at $9000).
        ASSERT_EQ(mem_->read(0x9000), 'M');
        ASSERT_EQ(mem_->read(0x9001), 'F');
        ASSERT_EQ(mem_->read(0x9002), 'C');
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(image_path_, ec);
    }

    // Drive a sector write at the C++ register level (the documented sequence),
    // independent of the 6502 routine under test.
    void cppWriteSector(uint16_t lba, const std::array<uint8_t, kSectorSize> &data) {
        mem_->write(BlockDevice::kRegLbaLo, static_cast<uint8_t>(lba & 0xFF));
        mem_->write(BlockDevice::kRegLbaHi, static_cast<uint8_t>(lba >> 8));
        for (size_t i = 0; i < kSectorSize; ++i)
            mem_->write(BlockDevice::kRegData, data[i]);
        mem_->write(BlockDevice::kRegCmd, BlockDevice::kCmdWriteSector);
    }

    std::array<uint8_t, kSectorSize> cppReadSector(uint16_t lba) {
        mem_->write(BlockDevice::kRegLbaLo, static_cast<uint8_t>(lba & 0xFF));
        mem_->write(BlockDevice::kRegLbaHi, static_cast<uint8_t>(lba >> 8));
        mem_->write(BlockDevice::kRegCmd, BlockDevice::kCmdReadSector);
        std::array<uint8_t, kSectorSize> out{};
        for (size_t i = 0; i < kSectorSize; ++i)
            out[i] = mem_->read(BlockDevice::kRegData);
        return out;
    }

    // Point BLK_BUF_PTR ($3A/$3B) at a RAM buffer.
    void setBufPtr(uint16_t addr) {
        mem_->write(kBlkBufPtr, static_cast<uint8_t>(addr & 0xFF));
        mem_->write(kBlkBufPtr + 1, static_cast<uint8_t>(addr >> 8));
    }

    // Call a DOS routine via JSR semantics and run until it returns (PC leaves
    // the DOS ROM). Returns true if it completed; sets carry_out from the final
    // carry flag (the routines report errors via carry).
    bool callRoutine(uint16_t entry, uint8_t a, uint8_t x, bool &carry_out) {
        cpu_->reg.SP = 0xFF;
        // Push a return address of $FFFF (high then low, as JSR does) so the
        // routine's RTS returns to $0000 - safely outside the DOS ROM.
        cpu_->pushByte(0xFF);
        cpu_->pushByte(0xFF);
        cpu_->reg.PC = entry;
        cpu_->reg.A = a;
        cpu_->reg.X = x;
        for (int i = 0; i < 200000; ++i) {
            const uint16_t pc = cpu_->reg.PC;
            if (pc < Memory::kDosRomStart || pc > Memory::kDosRomEnd) {
                carry_out = cpu_->getFlag(CPU6502::kCarry);
                return true;
            }
            if (!cpu_->executeSingleInstruction())
                return false;
        }
        return false; // never returned
    }

    void ramFill(uint16_t addr, const std::array<uint8_t, kSectorSize> &data) {
        for (size_t i = 0; i < kSectorSize; ++i)
            mem_->write(addr + i, data[i]);
    }

    std::array<uint8_t, kSectorSize> ramRead(uint16_t addr) {
        std::array<uint8_t, kSectorSize> out{};
        for (size_t i = 0; i < kSectorSize; ++i)
            out[i] = mem_->read(addr + i);
        return out;
    }

    Computer6502 computer;
    Memory *mem_ = nullptr;
    CPU6502 *cpu_ = nullptr;
    std::string image_path_;
};

// The 6502 read primitive copies a full sector from disk into the RAM buffer.
TEST_F(DosBlockIoTest, ReadPrimitiveCopiesSectorToRam) {
    const auto pattern = makeSector(0x5A);
    cppWriteSector(9, pattern); // stage sector 9 on disk

    // Dirty the RAM buffer first, so a successful read must overwrite it.
    ramFill(kRamBuf, makeSector(0xFF));

    setBufPtr(kRamBuf);
    bool carry = true;
    ASSERT_TRUE(callRoutine(kBlkReadEntry, /*lba lo*/ 9, /*lba hi*/ 0, carry));
    EXPECT_FALSE(carry); // carry clear = success
    EXPECT_EQ(ramRead(kRamBuf), pattern);
}

// The 6502 write primitive copies a full sector from the RAM buffer to disk.
TEST_F(DosBlockIoTest, WritePrimitiveCopiesRamToSector) {
    const auto pattern = makeSector(0x33);
    ramFill(kRamBuf, pattern);

    setBufPtr(kRamBuf);
    bool carry = true;
    ASSERT_TRUE(callRoutine(kBlkWriteEntry, /*lba lo*/ 12, /*lba hi*/ 0, carry));
    EXPECT_FALSE(carry);

    EXPECT_EQ(cppReadSector(12), pattern); // verify via the C++ block device
}

// Round-trip entirely through the 6502 primitives (write then read back).
TEST_F(DosBlockIoTest, RoundTripThrough6502Primitives) {
    const auto pattern = makeSector(0xA7);
    ramFill(kRamBuf, pattern);

    setBufPtr(kRamBuf);
    bool carry = true;
    ASSERT_TRUE(callRoutine(kBlkWriteEntry, 1, 1, carry)); // sector $0101
    EXPECT_FALSE(carry);

    // Read it back into a different RAM buffer via the 6502 read primitive.
    const uint16_t readBuf = 0x1000;
    setBufPtr(readBuf);
    ASSERT_TRUE(callRoutine(kBlkReadEntry, 1, 1, carry));
    EXPECT_FALSE(carry);
    EXPECT_EQ(ramRead(readBuf), pattern);
}

// The buffer pointer is preserved across the call (high byte restored).
TEST_F(DosBlockIoTest, BufferPointerPreserved) {
    cppWriteSector(3, makeSector(0x01));
    setBufPtr(kRamBuf);
    bool carry = true;
    ASSERT_TRUE(callRoutine(kBlkReadEntry, 3, 0, carry));
    EXPECT_EQ(mem_->read(kBlkBufPtr), static_cast<uint8_t>(kRamBuf & 0xFF));
    EXPECT_EQ(mem_->read(kBlkBufPtr + 1), static_cast<uint8_t>(kRamBuf >> 8));
}

} // namespace
