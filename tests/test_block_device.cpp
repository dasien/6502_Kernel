/**
 * @file test_block_device.cpp
 * @brief Smoke tests for the MFC-DOS block device (phase 1 storage foundation).
 *
 * The block device backs a host `disk.img` and is reached by the 6502 through
 * four memory-mapped registers just past MODULE_BANK ($FE23):
 *
 *   $FE24-$FE25  BLK_LBA     16-bit sector number (little-endian)
 *   $FE26        BLK_CMD     write 1 = read sector, 2 = write sector
 *   $FE27        BLK_STATUS  0 = ready, non-zero = error
 *   $FE28        BLK_DATA    512-byte sector data port (auto-incrementing)
 *
 * These tests drive those registers *through Memory* exactly as 6502 code would
 * (set LBA, fill/drain BLK_DATA x512, issue BLK_CMD), so they exercise the same
 * dispatch path the CPU hits - a faithful "6502 sector read/write smoke test"
 * without needing a hand-assembled routine. The documented register sequences:
 *
 *   write:  set BLK_LBA; write BLK_DATA x512; write BLK_CMD=2
 *   read:   set BLK_LBA; write BLK_CMD=1; read BLK_DATA x512
 */

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "computer/BlockDevice.h"
#include "computer/Memory.h"

using Computer::BlockDevice;
using Computer::Memory;

namespace {

constexpr size_t kSectorSize = BlockDevice::kSectorSize; // 512

// A distinctive 512-byte sector pattern keyed by a per-sector seed, so a
// mismatched / mixed-up sector is obvious.
std::array<uint8_t, kSectorSize> makeSector(uint8_t seed) {
    std::array<uint8_t, kSectorSize> s{};
    for (size_t i = 0; i < kSectorSize; ++i) {
        s[i] = static_cast<uint8_t>((i * 7 + seed * 13) & 0xFF);
    }
    return s;
}

class BlockDeviceTest : public ::testing::Test {
protected:
    // Each test gets its own throwaway image path under the system temp dir.
    void SetUp() override {
        static int counter = 0;
        image_path_ = (std::filesystem::temp_directory_path() /
                       ("mfcdos_blockdev_test_" + std::to_string(++counter) + ".img"))
                          .string();
        std::error_code ec;
        std::filesystem::remove(image_path_, ec); // start clean
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(image_path_, ec);
    }

    // Write a sector via the documented register sequence.
    static void writeSector(Memory &mem, uint16_t lba,
                            const std::array<uint8_t, kSectorSize> &data) {
        mem.write(BlockDevice::kRegLbaLo, static_cast<uint8_t>(lba & 0xFF));
        mem.write(BlockDevice::kRegLbaHi, static_cast<uint8_t>(lba >> 8));
        for (size_t i = 0; i < kSectorSize; ++i) {
            mem.write(BlockDevice::kRegData, data[i]);
        }
        mem.write(BlockDevice::kRegCmd, BlockDevice::kCmdWriteSector);
    }

    // Read a sector via the documented register sequence.
    static std::array<uint8_t, kSectorSize> readSector(Memory &mem, uint16_t lba) {
        mem.write(BlockDevice::kRegLbaLo, static_cast<uint8_t>(lba & 0xFF));
        mem.write(BlockDevice::kRegLbaHi, static_cast<uint8_t>(lba >> 8));
        mem.write(BlockDevice::kRegCmd, BlockDevice::kCmdReadSector);
        std::array<uint8_t, kSectorSize> out{};
        for (size_t i = 0; i < kSectorSize; ++i) {
            out[i] = mem.read(BlockDevice::kRegData);
        }
        return out;
    }

    std::string image_path_;
};

// A sector written then read back through the registers round-trips exactly.
TEST_F(BlockDeviceTest, RoundTripSingleSector) {
    BlockDevice dev{image_path_};
    Memory mem{nullptr, nullptr};
    mem.setBlockDevice(&dev);

    const auto pattern = makeSector(0x5A);
    writeSector(mem, 5, pattern);
    EXPECT_EQ(mem.read(BlockDevice::kRegStatus), BlockDevice::kStatusReady);

    const auto got = readSector(mem, 5);
    EXPECT_EQ(mem.read(BlockDevice::kRegStatus), BlockDevice::kStatusReady);
    EXPECT_EQ(got, pattern);
}

// The data port auto-increments and wraps at 512: byte 512 reads as byte 0.
TEST_F(BlockDeviceTest, DataPortAutoIncrementWraps) {
    BlockDevice dev{image_path_};
    Memory mem{nullptr, nullptr};
    mem.setBlockDevice(&dev);

    const auto pattern = makeSector(0x11);
    writeSector(mem, 0, pattern);

    // Issue read; drain exactly one sector, then one more read should wrap.
    mem.write(BlockDevice::kRegLbaLo, 0);
    mem.write(BlockDevice::kRegLbaHi, 0);
    mem.write(BlockDevice::kRegCmd, BlockDevice::kCmdReadSector);
    for (size_t i = 0; i < kSectorSize; ++i) {
        EXPECT_EQ(mem.read(BlockDevice::kRegData), pattern[i]) << "at index " << i;
    }
    // 513th access wraps back to index 0.
    EXPECT_EQ(mem.read(BlockDevice::kRegData), pattern[0]);
}

// Data persists in the host image across a fresh device instance (reopen).
TEST_F(BlockDeviceTest, PersistsAcrossReopen) {
    const auto pattern = makeSector(0x77);
    {
        BlockDevice dev{image_path_};
        Memory mem{nullptr, nullptr};
        mem.setBlockDevice(&dev);
        writeSector(mem, 42, pattern);
        EXPECT_EQ(mem.read(BlockDevice::kRegStatus), BlockDevice::kStatusReady);
    }

    BlockDevice dev2{image_path_};
    Memory mem2{nullptr, nullptr};
    mem2.setBlockDevice(&dev2);
    EXPECT_EQ(readSector(mem2, 42), pattern);
}

// Distinct sectors stay independent (no cross-talk / offset errors).
TEST_F(BlockDeviceTest, MultipleSectorsIndependent) {
    BlockDevice dev{image_path_};
    Memory mem{nullptr, nullptr};
    mem.setBlockDevice(&dev);

    const auto a = makeSector(0x01);
    const auto b = makeSector(0xFE);
    writeSector(mem, 0, a);
    writeSector(mem, 100, b);

    EXPECT_EQ(readSector(mem, 0), a);
    EXPECT_EQ(readSector(mem, 100), b);
}

// An unwritten sector reads back as all zeros with a ready status (a fresh disk
// behaves as a zeroed volume rather than an error).
TEST_F(BlockDeviceTest, FreshSectorReadsAsZero) {
    BlockDevice dev{image_path_};
    Memory mem{nullptr, nullptr};
    mem.setBlockDevice(&dev);

    const auto got = readSector(mem, 7);
    EXPECT_EQ(mem.read(BlockDevice::kRegStatus), BlockDevice::kStatusReady);
    for (size_t i = 0; i < kSectorSize; ++i) {
        EXPECT_EQ(got[i], 0x00) << "at index " << i;
    }
}

// Setting BLK_LBA resets the data-port index, so a fill always starts at byte 0
// even after a partial drain.
TEST_F(BlockDeviceTest, SettingLbaResetsDataIndex) {
    BlockDevice dev{image_path_};
    Memory mem{nullptr, nullptr};
    mem.setBlockDevice(&dev);

    // Partially advance the index by reading a few bytes of sector 0.
    mem.write(BlockDevice::kRegLbaLo, 0);
    mem.write(BlockDevice::kRegLbaHi, 0);
    mem.write(BlockDevice::kRegCmd, BlockDevice::kCmdReadSector);
    (void)mem.read(BlockDevice::kRegData);
    (void)mem.read(BlockDevice::kRegData);

    // Re-selecting an LBA must rewind the port to index 0 for the next fill.
    const auto pattern = makeSector(0x33);
    writeSector(mem, 3, pattern);
    EXPECT_EQ(readSector(mem, 3), pattern);
}

// LBA registers read back what was written; status defaults to ready.
TEST_F(BlockDeviceTest, RegistersReadBack) {
    BlockDevice dev{image_path_};
    Memory mem{nullptr, nullptr};
    mem.setBlockDevice(&dev);

    mem.write(BlockDevice::kRegLbaLo, 0x34);
    mem.write(BlockDevice::kRegLbaHi, 0x12);
    EXPECT_EQ(mem.read(BlockDevice::kRegLbaLo), 0x34);
    EXPECT_EQ(mem.read(BlockDevice::kRegLbaHi), 0x12);
    EXPECT_EQ(mem.read(BlockDevice::kRegStatus), BlockDevice::kStatusReady);
}

// A write to an uncreatable path reports the error status rather than crashing.
TEST_F(BlockDeviceTest, ErrorStatusOnUnwritablePath) {
    const std::string bad =
        (std::filesystem::path("/this_dir_should_not_exist_mfcdos") / "disk.img")
            .string();
    BlockDevice dev{bad};
    Memory mem{nullptr, nullptr};
    mem.setBlockDevice(&dev);

    const auto pattern = makeSector(0x99);
    writeSector(mem, 1, pattern);
    EXPECT_EQ(mem.read(BlockDevice::kRegStatus), BlockDevice::kStatusError);
}

} // namespace
