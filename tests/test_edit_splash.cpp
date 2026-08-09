/**
 * @file test_edit_splash.cpp
 * @brief EDIT's title card, on the real 6502 blob.
 *
 * EDIT had no test suite at all, which mattered the moment its glue grew vfill and
 * vcmd for the chip-side clear the card uses: a mistake there would have shown up
 * only when somebody launched the editor and found the screen full of rubbish.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "computer/Computer6502.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
#include "computer/VIC.h"

namespace {

std::string rowText(Computer::Computer6502 &box, int y)
{
    std::string t;
    for (int x = 0; x < 80; ++x) {
        const uint8_t ch = box.getVideoChip()->getCharacterAt(x, y);
        t.push_back((ch >= 32 && ch < 127) ? static_cast<char>(ch) : ' ');
    }
    return t;
}

TEST(EditSplash, TitleCardShowsAtStartup)
{
    Computer::Computer6502 box;
    box.power_on();

    std::ifstream f("../kernel/edit.bin", std::ios::binary);
    ASSERT_TRUE(f.good()) << "edit.bin not found - build the edit_bin target";
    const std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    for (size_t i = 0; i < blob.size(); ++i)
        box.getMemory()->write(static_cast<uint16_t>(0x0800 + i), blob[i]);

    auto *cpu = box.getCpu();
    cpu->reg.SP = 0xFF;
    cpu->pushByte(0xFF);
    cpu->pushByte(0xFF);
    cpu->reg.PC = 0x0800;
    // Entering at $0800 skips the kernel boot, which is where the I flag gets
    // cleared. Without this the timer IRQ never lands -- harmless here, since the
    // card is supposed to still be up, but it is the same trap the other harnesses
    // hit and worth not re-learning.
    cpu->setFlag(Computer::CPU6502::kInterrupt, false);

    // No timer pulses, so the three seconds never elapse and the card stays put.
    for (int i = 0; i < 200000; ++i)
        if (!cpu->executeSingleInstruction()) break;

    EXPECT_NE(rowText(box, 10).find("M F C   E D I T O R"), std::string::npos)
        << "no title on row 10, got: [" << rowText(box, 10) << "]";
    EXPECT_NE(rowText(box, 12).find("FULL-SCREEN TEXT EDITOR"), std::string::npos)
        << "no subtitle on row 12, got: [" << rowText(box, 12) << "]";
}

} // namespace
