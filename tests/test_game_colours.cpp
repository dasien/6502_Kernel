/**
 * @file test_game_colours.cpp
 * @brief VAULT and FRONTIER state their own background.
 *
 * Attributes name palette slots, so a theme loaded by the shell reaches into
 * any program that has not said otherwise. A program with an opinion about its
 * colours loads a palette at startup; one without inherits, which is what you
 * want from anything that is mostly text.
 *
 * VENTURE and KPANIC have their own harnesses and assert this there. These two
 * have none, so the calls went in untested -- and what is at risk is not the
 * mechanism, which is proven twice over, but the call still being PRESENT after
 * someone rearranges main(). That is what these hold shut.
 *
 * Deliberately not a fixture: the theme has to be loaded before the first
 * instruction, so each test owns its machine. Reading the VIC rather than the
 * games' own variables is also why neither blob needs a label file.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "computer/CPU6502.h"
#include "computer/Computer6502.h"
#include "computer/Memory.h"
#include "computer/VIC.h"

namespace {

// The kernel's SOUND_ENABLE flag. Boot sets it; a headless harness has to.
constexpr uint16_t kSoundEnable = 0x0029;

/* Load a $0800 blob, put a theme in the palette, and run its startup.
 *
 * The theme goes in BEFORE the first instruction, because that is the thing
 * under test -- a test that loads one afterwards is measuring its own write.
 * Returns the background slot after startup. */
void runStartupUnderATheme(const char *blob_path, uint8_t &r, uint8_t &g, uint8_t &b)
{
    Computer::Computer6502 box;
    box.power_on();

    std::ifstream f(blob_path, std::ios::binary);
    ASSERT_TRUE(f.good()) << blob_path << " not found - build its _bin target";
    const std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    ASSERT_GE(blob.size(), 0x100u);
    for (size_t i = 0; i < blob.size(); ++i)
        box.getMemory()->write(static_cast<uint16_t>(0x0800 + i), blob[i]);

    // A theme is already loaded, exactly as arriving from a themed prompt.
    box.getMemory()->write(Computer::VIC::kRegPaletteIdx, 0);
    box.getMemory()->write(Computer::VIC::kRegPaletteData, 0x2c);
    box.getMemory()->write(Computer::VIC::kRegPaletteData, 0x1c);
    box.getMemory()->write(Computer::VIC::kRegPaletteData, 0x15);

    uint8_t cr = 0, cg = 0, cb = 0;
    box.getVideoChip()->paletteColor(0, cr, cg, cb);
    ASSERT_EQ(cr, 0x2c) << "the stand-in theme did not load";

    box.getCpu()->reg.SP = 0xFF;
    box.getCpu()->pushByte(0xFF);
    box.getCpu()->pushByte(0xFF);
    box.getCpu()->reg.PC = 0x0800;
    box.getCpu()->setFlag(Computer::CPU6502::kInterrupt, false);
    box.getMemory()->write(kSoundEnable, 0x01);

    box.runInstructions(300000);        // startup is all this needs

    box.getVideoChip()->paletteColor(0, r, g, b);
}

/* A vault is meant to be unlit and underground; A_FLOOR is the default pair, so
   without this the dungeon would be lit by whatever the prompt was wearing. */
TEST(GameColours, VaultStatesABlackBackground)
{
    uint8_t r = 0xff, g = 0xff, b = 0xff;
    runStartupUnderATheme("../kernel/vault.bin", r, g, b);
    EXPECT_EQ(r, 0x00); EXPECT_EQ(g, 0x00); EXPECT_EQ(b, 0x00)
        << "VAULT inherited the theme's background instead of stating its own";
}

/* The ledger should read as ink on a dark counter whatever the machine wears. */
TEST(GameColours, FrontierStatesABlackBackground)
{
    uint8_t r = 0xff, g = 0xff, b = 0xff;
    runStartupUnderATheme("../kernel/frontier.bin", r, g, b);
    EXPECT_EQ(r, 0x00); EXPECT_EQ(g, 0x00); EXPECT_EQ(b, 0x00)
        << "FRONTIER inherited the theme's background instead of stating its own";
}

} // namespace
