/**
 * @file test_dos_fat16.cpp
 * @brief Exercises the MFC-DOS FAT16 driver (phase 2, step 2.3) against a built
 *        FAT16 image, by running the real dos.rom 6502 routines.
 *
 * 2.3a covers mount (BPB parse) + directory enumeration: FS_DIR_FIRST ($AF0F)
 * and FS_DIR_NEXT ($AF12) walk the root directory, leaving each 32-byte entry in
 * the DOS state block at DOS_ENTRY ($0320). The image is constructed by the
 * host-side Fat16ImageBuilder, so a match validates the driver end-to-end.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "computer/BlockDevice.h"
#include "computer/Computer6502.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
#include "support/fat16_image.h"

using Computer::BlockDevice;
using Computer::Computer6502;
using Computer::CPU6502;
using Computer::Memory;
using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;

namespace {

// DOS ABI jump-table entry points (fixed addresses; see dos.asm).
constexpr uint16_t kFsOpen = 0xAF03;
constexpr uint16_t kFsGetb = 0xAF06;
constexpr uint16_t kFsPutb = 0xAF09;
constexpr uint16_t kFsClose = 0xAF0C;
constexpr uint16_t kFsDirFirst = 0xAF0F;
constexpr uint16_t kFsDirNext = 0xAF12;
constexpr uint16_t kFsDelete = 0xAF1B;

// A RAM scratch address (in user RAM, untouched by the FS) for filename strings.
constexpr uint16_t kNameAddr = 0x0800;

// DOS state block.
constexpr uint16_t kDosEntry = 0x0320;     // 32-byte current directory entry
constexpr uint16_t kDirSizeOff = 0x1C;     // size field within the entry

struct DirEntry {
    std::string name; // decoded "NAME.EXT"
    uint32_t size;
};

// Decode an 11-byte 8.3 directory name (space-padded) into "NAME.EXT".
std::string decode83(const std::array<uint8_t, 11> &raw) {
    std::string base, ext;
    for (int i = 0; i < 8; ++i)
        if (raw[i] != ' ') base.push_back(static_cast<char>(raw[i]));
    for (int i = 8; i < 11; ++i)
        if (raw[i] != ' ') ext.push_back(static_cast<char>(raw[i]));
    return ext.empty() ? base : base + "." + ext;
}

class DosFat16Test : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        image_path_ = (std::filesystem::temp_directory_path() /
                       ("mfcdos_fat16_test_" + std::to_string(++counter) + ".img"))
                          .string();
        computer.power_on();
        mem_ = computer.getMemory();
        cpu_ = computer.getCpu();
        computer.getBlockDevice()->setImagePath(image_path_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(image_path_, ec);
    }

    void writeImage(const std::vector<Fat16File> &files) {
        const std::vector<uint8_t> img = Fat16ImageBuilder::build(files);
        std::ofstream f(image_path_, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(img.data()),
                static_cast<std::streamsize>(img.size()));
    }

    // Run a DOS routine to completion (RTS leaves the DOS ROM). Returns carry.
    bool callRoutine(uint16_t entry, bool &carry_out, uint8_t a = 0, uint8_t x = 0,
                     uint8_t y = 0) {
        cpu_->reg.SP = 0xFF;
        cpu_->pushByte(0xFF);
        cpu_->pushByte(0xFF); // return address $FFFF -> RTS to $0000
        cpu_->reg.PC = entry;
        cpu_->reg.A = a;
        cpu_->reg.X = x;
        cpu_->reg.Y = y;
        for (int i = 0; i < 2000000; ++i) {
            const uint16_t pc = cpu_->reg.PC;
            if (pc < Memory::kDosRomStart || pc > Memory::kDosRomEnd) {
                carry_out = cpu_->getFlag(CPU6502::kCarry);
                return true;
            }
            if (!cpu_->executeSingleInstruction()) return false;
        }
        return false;
    }

    DirEntry readEntry() {
        std::array<uint8_t, 11> raw{};
        for (int i = 0; i < 11; ++i) raw[i] = mem_->read(kDosEntry + i);
        uint32_t size = 0;
        for (int i = 0; i < 4; ++i)
            size |= static_cast<uint32_t>(mem_->read(kDosEntry + kDirSizeOff + i)) << (8 * i);
        return {decode83(raw), size};
    }

    // Enumerate the whole directory via FS_DIR_FIRST / FS_DIR_NEXT.
    std::vector<DirEntry> enumerate() {
        std::vector<DirEntry> out;
        bool carry = true;
        if (!callRoutine(kFsDirFirst, carry) || carry) return out; // empty
        out.push_back(readEntry());
        for (;;) {
            if (!callRoutine(kFsDirNext, carry) || carry) break;
            out.push_back(readEntry());
        }
        return out;
    }

    // Open a file by name, read it to EOF via FS_GETB, then FS_CLOSE.
    // Returns true if FS_OPEN succeeded; the contents are returned in `out`.
    bool openReadClose(const std::string &name, std::vector<uint8_t> &out) {
        out.clear();
        for (size_t i = 0; i < name.size(); ++i)
            mem_->write(kNameAddr + i, static_cast<uint8_t>(name[i]));
        mem_->write(kNameAddr + name.size(), 0); // null terminator

        bool carry = true;
        if (!callRoutine(kFsOpen, carry, kNameAddr & 0xFF, kNameAddr >> 8) || carry)
            return false; // not found / not mounted

        for (;;) {
            if (!callRoutine(kFsGetb, carry)) break; // runaway guard
            if (carry) break;                        // EOF
            out.push_back(cpu_->reg.A);
        }
        callRoutine(kFsClose, carry);
        return true;
    }

    // Write a file through the 6502 FS write path: FS_OPEN(write) + FS_PUTB per
    // byte + FS_CLOSE. Returns true on success.
    bool fsWriteFile(const std::string &name, const std::vector<uint8_t> &data) {
        for (size_t i = 0; i < name.size(); ++i)
            mem_->write(kNameAddr + i, static_cast<uint8_t>(name[i]));
        mem_->write(kNameAddr + name.size(), 0);

        bool carry = true;
        if (!callRoutine(kFsOpen, carry, kNameAddr & 0xFF, kNameAddr >> 8, /*mode=*/1) || carry)
            return false;
        for (uint8_t b : data) {
            if (!callRoutine(kFsPutb, carry, b) || carry) return false;
        }
        if (!callRoutine(kFsClose, carry) || carry) return false;
        return true;
    }

    // Delete a file through the 6502 FS_DELETE path. Returns true on success.
    bool fsDelete(const std::string &name) {
        for (size_t i = 0; i < name.size(); ++i)
            mem_->write(kNameAddr + i, static_cast<uint8_t>(name[i]));
        mem_->write(kNameAddr + name.size(), 0);
        bool carry = true;
        return callRoutine(kFsDelete, carry, kNameAddr & 0xFF, kNameAddr >> 8) && !carry;
    }

    // Read the current on-disk image back from the host file.
    std::vector<uint8_t> readImageFile() {
        std::ifstream f(image_path_, std::ios::binary | std::ios::ate);
        std::streamsize n = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> img(n > 0 ? static_cast<size_t>(n) : 0);
        if (n > 0) f.read(reinterpret_cast<char *>(img.data()), n);
        return img;
    }

    Computer6502 computer;
    Memory *mem_ = nullptr;
    CPU6502 *cpu_ = nullptr;
    std::string image_path_;
};

TEST_F(DosFat16Test, EnumeratesAllFilesWithSizes) {
    writeImage({
        {"HELLO.TXT", std::vector<uint8_t>(15, 'H')},
        {"README", std::vector<uint8_t>(40, 'R')},
        {"DATA.BIN", std::vector<uint8_t>(600, 0xAB)}, // spans 2 clusters
        {"EMPTY.TXT", {}},
    });

    const auto entries = enumerate();
    ASSERT_EQ(entries.size(), 4u);
    EXPECT_EQ(entries[0].name, "HELLO.TXT");
    EXPECT_EQ(entries[0].size, 15u);
    EXPECT_EQ(entries[1].name, "README");
    EXPECT_EQ(entries[1].size, 40u);
    EXPECT_EQ(entries[2].name, "DATA.BIN");
    EXPECT_EQ(entries[2].size, 600u);
    EXPECT_EQ(entries[3].name, "EMPTY.TXT");
    EXPECT_EQ(entries[3].size, 0u);
}

TEST_F(DosFat16Test, EmptyDirectoryYieldsNothing) {
    writeImage({});
    EXPECT_TRUE(enumerate().empty());
}

TEST_F(DosFat16Test, EnumerationIsRepeatable) {
    writeImage({{"ONE.TXT", std::vector<uint8_t>(10, '1')},
                {"TWO.TXT", std::vector<uint8_t>(20, '2')}});
    const auto first = enumerate();
    const auto second = enumerate(); // FS_DIR_FIRST must rewind
    ASSERT_EQ(first.size(), 2u);
    ASSERT_EQ(second.size(), 2u);
    EXPECT_EQ(first[0].name, second[0].name);
    EXPECT_EQ(first[1].name, second[1].name);
}

// --- File read (FS_OPEN / FS_GETB / FS_CLOSE) -----------------------------

// A deterministic byte pattern so multi-cluster reads are meaningfully checked.
std::vector<uint8_t> pattern(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<uint8_t>((i * 31 + seed) & 0xFF);
    return v;
}

TEST_F(DosFat16Test, ReadsSmallFile) {
    const std::vector<uint8_t> content = {'H', 'i', ' ', 'D', 'O', 'S', '!'};
    writeImage({{"HELLO.TXT", content}});

    std::vector<uint8_t> out;
    ASSERT_TRUE(openReadClose("HELLO.TXT", out));
    EXPECT_EQ(out, content);
}

TEST_F(DosFat16Test, OpenIsCaseInsensitive) {
    const std::vector<uint8_t> content = {'a', 'b', 'c'};
    writeImage({{"READ.ME", content}});
    std::vector<uint8_t> out;
    ASSERT_TRUE(openReadClose("read.me", out)); // lowercase request
    EXPECT_EQ(out, content);
}

TEST_F(DosFat16Test, ReadsEmptyFile) {
    writeImage({{"EMPTY.TXT", {}}});
    std::vector<uint8_t> out;
    ASSERT_TRUE(openReadClose("EMPTY.TXT", out)); // opens OK
    EXPECT_TRUE(out.empty());                     // immediate EOF
}

TEST_F(DosFat16Test, OpenNonexistentFails) {
    writeImage({{"REAL.TXT", {'x'}}});
    std::vector<uint8_t> out;
    EXPECT_FALSE(openReadClose("NOPE.TXT", out));
}

TEST_F(DosFat16Test, ReadsExactlyOneCluster) {
    const auto content = pattern(512, 0x40); // exactly one 512-byte cluster
    writeImage({{"ONE.BIN", content}});
    std::vector<uint8_t> out;
    ASSERT_TRUE(openReadClose("ONE.BIN", out));
    EXPECT_EQ(out, content);
}

TEST_F(DosFat16Test, ReadsMultiClusterFile) {
    const auto content = pattern(600, 0x11); // spans 2 clusters
    writeImage({{"DATA.BIN", content}});
    std::vector<uint8_t> out;
    ASSERT_TRUE(openReadClose("DATA.BIN", out));
    EXPECT_EQ(out, content);
}

TEST_F(DosFat16Test, ReadsFileSpanningSeveralClusters) {
    const auto content = pattern(2000, 0x9C); // 4 clusters, partial last
    writeImage({{"BIG.DAT", content}});
    std::vector<uint8_t> out;
    ASSERT_TRUE(openReadClose("BIG.DAT", out));
    ASSERT_EQ(out.size(), content.size());
    EXPECT_EQ(out, content);
}

TEST_F(DosFat16Test, ReadsSecondFileAfterFirst) {
    const auto a = pattern(300, 0x01);
    const auto b = pattern(900, 0x02);
    writeImage({{"A.BIN", a}, {"B.BIN", b}});
    std::vector<uint8_t> oa, ob;
    ASSERT_TRUE(openReadClose("A.BIN", oa));
    ASSERT_TRUE(openReadClose("B.BIN", ob));
    EXPECT_EQ(oa, a);
    EXPECT_EQ(ob, b);
}

// --- File write (FS_OPEN write / FS_PUTB / FS_CLOSE) ----------------------

using mfcdos_test::Fat16ImageReader;

TEST_F(DosFat16Test, WritesAndReadsBackSmallFile) {
    writeImage({}); // format an empty volume
    const auto content = pattern(20, 0x42);
    ASSERT_TRUE(fsWriteFile("OUT.TXT", content));

    // (a) verify the on-disk image is valid FAT16 to an independent parser.
    Fat16ImageReader reader(readImageFile());
    Fat16ImageReader::Entry e;
    ASSERT_TRUE(reader.find("OUT.TXT", e));
    EXPECT_EQ(e.size, 20u);
    EXPECT_GE(e.firstCluster, 2u);
    std::vector<uint8_t> parsed;
    ASSERT_TRUE(reader.read("OUT.TXT", parsed));
    EXPECT_EQ(parsed, content);

    // (b) verify the 6502 read path round-trips it too.
    std::vector<uint8_t> readback;
    ASSERT_TRUE(openReadClose("OUT.TXT", readback));
    EXPECT_EQ(readback, content);
}

TEST_F(DosFat16Test, WritesEmptyFile) {
    writeImage({});
    ASSERT_TRUE(fsWriteFile("EMPTY.TXT", {}));
    Fat16ImageReader reader(readImageFile());
    Fat16ImageReader::Entry e;
    ASSERT_TRUE(reader.find("EMPTY.TXT", e));
    EXPECT_EQ(e.size, 0u);
    std::vector<uint8_t> rb;
    ASSERT_TRUE(openReadClose("EMPTY.TXT", rb));
    EXPECT_TRUE(rb.empty());
}

TEST_F(DosFat16Test, WritesMultiClusterFile) {
    writeImage({});
    const auto content = pattern(1500, 0x07); // 3 clusters (512 each)
    ASSERT_TRUE(fsWriteFile("BIG.DAT", content));

    Fat16ImageReader reader(readImageFile());
    std::vector<uint8_t> parsed;
    ASSERT_TRUE(reader.read("BIG.DAT", parsed));
    EXPECT_EQ(parsed, content);
    // 1500 bytes / 512 = 3 clusters allocated.
    EXPECT_EQ(reader.allocatedClusters(), 3);
}

TEST_F(DosFat16Test, WritesTwoFilesIndependently) {
    writeImage({});
    const auto a = pattern(700, 0x10);  // 2 clusters
    const auto b = pattern(300, 0x20);  // 1 cluster
    ASSERT_TRUE(fsWriteFile("A.BIN", a));
    ASSERT_TRUE(fsWriteFile("B.BIN", b));

    Fat16ImageReader reader(readImageFile());
    std::vector<uint8_t> pa, pb;
    ASSERT_TRUE(reader.read("A.BIN", pa));
    ASSERT_TRUE(reader.read("B.BIN", pb));
    EXPECT_EQ(pa, a);
    EXPECT_EQ(pb, b);
    EXPECT_EQ(reader.allocatedClusters(), 3); // 2 + 1, no overlap/leak
}

TEST_F(DosFat16Test, OverwriteTruncatesAndFreesOldChain) {
    writeImage({});
    ASSERT_TRUE(fsWriteFile("F.DAT", pattern(2000, 0x55))); // 4 clusters
    const auto small = pattern(100, 0xAA);                   // 1 cluster
    ASSERT_TRUE(fsWriteFile("F.DAT", small));                // same name -> truncate

    Fat16ImageReader reader(readImageFile());
    Fat16ImageReader::Entry e;
    ASSERT_TRUE(reader.find("F.DAT", e));
    EXPECT_EQ(e.size, 100u);
    std::vector<uint8_t> parsed;
    ASSERT_TRUE(reader.read("F.DAT", parsed));
    EXPECT_EQ(parsed, small);
    // Only the new single cluster should remain allocated (old 4 freed).
    EXPECT_EQ(reader.allocatedClusters(), 1);
    // And there must be exactly one directory entry for the name.
    int count = 0;
    for (const auto &de : reader.entries()) if (de.name == "F.DAT") ++count;
    EXPECT_EQ(count, 1);
}

TEST_F(DosFat16Test, WrittenFileAppearsInCatalogEnumeration) {
    writeImage({});
    ASSERT_TRUE(fsWriteFile("HELLO.TXT", pattern(10, 1)));
    ASSERT_TRUE(fsWriteFile("WORLD.TXT", pattern(10, 2)));
    const auto entries = enumerate(); // 6502 directory walk
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, "HELLO.TXT");
    EXPECT_EQ(entries[1].name, "WORLD.TXT");
}

// --- Erase (FS_DELETE) ----------------------------------------------------

TEST_F(DosFat16Test, EraseRemovesFileAndFreesClusters) {
    writeImage({});
    ASSERT_TRUE(fsWriteFile("DEL.DAT", pattern(700, 0x09)));   // 2 clusters
    ASSERT_TRUE(fsWriteFile("KEEP.TXT", pattern(50, 0x03)));   // 1 cluster
    ASSERT_TRUE(fsDelete("DEL.DAT"));

    // Gone from the 6502 directory walk; KEEP.TXT remains.
    const auto entries = enumerate();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "KEEP.TXT");

    // Independent parser: DEL.DAT gone, only KEEP's single cluster allocated.
    Fat16ImageReader reader(readImageFile());
    Fat16ImageReader::Entry e;
    EXPECT_FALSE(reader.find("DEL.DAT", e));
    EXPECT_EQ(reader.allocatedClusters(), 1);

    // KEEP.TXT still reads back correctly via the 6502 path.
    std::vector<uint8_t> rb;
    ASSERT_TRUE(openReadClose("KEEP.TXT", rb));
    EXPECT_EQ(rb, pattern(50, 0x03));
}

TEST_F(DosFat16Test, DeleteNonexistentFails) {
    writeImage({});
    ASSERT_TRUE(fsWriteFile("REAL.TXT", pattern(8, 1)));
    EXPECT_FALSE(fsDelete("NOPE.TXT"));
}

TEST_F(DosFat16Test, FreedClustersAreReused) {
    writeImage({});
    ASSERT_TRUE(fsWriteFile("BIG.DAT", pattern(1500, 0x11))); // 3 clusters
    ASSERT_TRUE(fsDelete("BIG.DAT"));
    ASSERT_TRUE(fsWriteFile("NEW.DAT", pattern(1500, 0x22))); // 3 clusters again
    Fat16ImageReader reader(readImageFile());
    std::vector<uint8_t> parsed;
    ASSERT_TRUE(reader.read("NEW.DAT", parsed));
    EXPECT_EQ(parsed, pattern(1500, 0x22));
    EXPECT_EQ(reader.allocatedClusters(), 3); // old chain was freed and reused
}

TEST_F(DosFat16Test, SkipsDeletedEntries) {
    // Build a normal image, then mark the first root entry deleted ($E5) and
    // confirm the driver skips it.
    auto img = Fat16ImageBuilder::build(
        {{"GONE.TXT", std::vector<uint8_t>(5, 'X')},
         {"KEEP.TXT", std::vector<uint8_t>(5, 'Y')}});
    // Root directory is at sector (reserved + numFats*fatSize); compute as the
    // builder does: reserved(1) + 1*fatSize. Re-derive via a fresh build's BPB.
    const uint16_t reserved = img[0x0E] | (img[0x0F] << 8);
    const uint8_t numFats = img[0x10];
    const uint16_t fatSize = img[0x16] | (img[0x17] << 8);
    const uint32_t rootSector = reserved + numFats * fatSize;
    img[rootSector * 512 + 0] = 0xE5; // delete "GONE.TXT"

    std::ofstream f(image_path_, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(img.data()),
            static_cast<std::streamsize>(img.size()));
    f.close();

    const auto entries = enumerate();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "KEEP.TXT");
}

} // namespace
