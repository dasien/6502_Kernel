/**
 * @file test_fat16_roundtrip.cpp
 * @brief Fat16ImageReader::readAll round-trip (root + drawers) — the core the
 *        mkdisk `read`/`update` modes rely on.
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "support/fat16_image.h"

using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;
using mfcdos_test::Fat16ImageReader;

namespace {
std::vector<uint8_t> bytesOf(const std::string &s) { return {s.begin(), s.end()}; }

const Fat16File *find(const std::vector<Fat16File> &v, const std::string &drawer,
                      const std::string &name) {
    for (const auto &f : v)
        if (f.drawer == drawer && f.name == name) return &f;
    return nullptr;
}
} // namespace

// readAll() returns every file (root + one-level drawers) with correct contents.
TEST(Fat16Roundtrip, ReadsRootAndDrawers) {
    const std::vector<Fat16File> in = {
        {"EDIT.PRG",   bytesOf("edit-body"),           ""},
        {"DIAL.LST",   bytesOf("bbs.host:23 A BBS\n"), "SYSTEM"},
        {"IRC.LST",    bytesOf("irc.host:6667 Net\n"), "SYSTEM"},
        {"CHESS.PRG",  bytesOf("chess"),               "GAMES"},
        {"ADVLAND.PRG",bytesOf(std::string(600, 'A')), "GAMES"},  // spans >1 cluster
    };
    const auto img = Fat16ImageBuilder::build(in, Fat16ImageBuilder::kHostFat16Clusters);
    Fat16ImageReader reader(img);
    const auto out = reader.readAll();

    ASSERT_EQ(out.size(), in.size());
    for (const auto &want : in) {
        const Fat16File *got = find(out, want.drawer, want.name);
        ASSERT_NE(got, nullptr) << "missing " << want.drawer << "/" << want.name;
        EXPECT_EQ(got->data, want.data) << want.drawer << "/" << want.name;
    }
}

// A drawer with more entries than fit in one cluster (>14 files) builds and
// round-trips intact -- the multi-cluster directory case (#79). 40 files + '.'/'..'
// = 42 entries = 3 clusters of 16.
TEST(Fat16Roundtrip, DrawerSpansMultipleClusters) {
    std::vector<Fat16File> in = {{"EDIT.PRG", bytesOf("edit"), ""}};
    const int kFiles = 40;
    for (int i = 0; i < kFiles; ++i)
        in.push_back({"GAME" + std::to_string(i) + ".PRG",
                      bytesOf("body-" + std::to_string(i)), "GAMES"});
    const auto img = Fat16ImageBuilder::build(in, Fat16ImageBuilder::kHostFat16Clusters);
    Fat16ImageReader reader(img);
    const auto out = reader.readAll();

    ASSERT_EQ(out.size(), in.size());
    for (const auto &want : in) {
        const Fat16File *got = find(out, want.drawer, want.name);
        ASSERT_NE(got, nullptr) << "missing " << want.drawer << "/" << want.name;
        EXPECT_EQ(got->data, want.data) << want.drawer << "/" << want.name;
    }
}

// Reader -> readAll -> Builder reproduces the image byte-for-byte (what mkdisk
// `read` then `create` does; also the basis for `update`).
TEST(Fat16Roundtrip, RebuildFromReadAllIsIdentical) {
    const std::vector<Fat16File> in = {
        {"EDIT.PRG",  bytesOf("edit"), ""},
        {"TERM.PRG",  bytesOf("term"), ""},
        {"DIAL.LST",  bytesOf("bbs\n"), "SYSTEM"},
        {"CHESS.PRG", bytesOf("chess"), "GAMES"},
    };
    const auto img1 = Fat16ImageBuilder::build(in, Fat16ImageBuilder::kHostFat16Clusters);
    Fat16ImageReader reader(img1);
    const auto img2 = Fat16ImageBuilder::build(reader.readAll(),
                                               Fat16ImageBuilder::kHostFat16Clusters);
    EXPECT_EQ(img1, img2);
}
