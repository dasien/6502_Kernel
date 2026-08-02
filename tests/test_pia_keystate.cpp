// Unit tests for the PIA's live key-state register ($FE0F).
//
// This register exists because the keystroke FIFO carries no key-up: an action
// game polling it can't tell a held key from a released one, and OS auto-repeat
// (the only "still held" signal) stalls ~500ms and drops the older key when a
// second is pressed. The register reports which control keys are down right now.
//
// Device-only: no Qt, no CPU, no Memory. The host input layer calls setKeyState;
// these tests call it directly, exactly as the GUI would.

#include "computer/PIA.h"

#include <gtest/gtest.h>

using Computer::PIA;

namespace
{
    constexpr uint16_t kKeyStateAddr = PIA::kPiaMemoryStart + PIA::kKeyState;
} // namespace

TEST(PiaKeyStateTest, AddressIsInsidePiaWindowAndPinned)
{
    PIA pia; // isPiaAddress is a non-static member on this device
    // Pin the literal address. The register sits in a hole between the timer ack
    // ($FE0E) and the file-I/O block ($FE10); if either grows into it the guest's
    // hard-coded read would silently start returning something else.
    EXPECT_EQ(kKeyStateAddr, 0xFE0Fu);
    EXPECT_TRUE(pia.isPiaAddress(kKeyStateAddr));

    // It must not collide with its neighbours.
    EXPECT_NE(PIA::kKeyState, PIA::kTimerIrqAck);
    EXPECT_NE(PIA::kKeyState, PIA::kFileCommand);
    EXPECT_EQ(PIA::kTimerIrqAck + 1, PIA::kKeyState);
    EXPECT_EQ(PIA::kKeyState + 1, PIA::kFileCommand);
}

TEST(PiaKeyStateTest, BitsAreDistinctAndFitOneByte)
{
    const uint8_t all = PIA::kKeyUp | PIA::kKeyDown | PIA::kKeyLeft |
                        PIA::kKeyRight | PIA::kKeyFire | PIA::kKeyButton2;
    // Six distinct single-bit flags, leaving bits 6-7 reserved.
    EXPECT_EQ(all, 0x3Fu);
}

TEST(PiaKeyStateTest, DefaultsToNothingHeld)
{
    // The console build and every headless test drive input through addKeypress
    // and never touch key state; they must read "no keys held" rather than junk.
    PIA pia;
    EXPECT_EQ(pia.keyState(), 0x00u);
    EXPECT_EQ(pia.readPia(kKeyStateAddr), 0x00u);
}

TEST(PiaKeyStateTest, RoundTripsThroughTheRegister)
{
    PIA pia;
    pia.setKeyState(PIA::kKeyLeft | PIA::kKeyFire);
    EXPECT_EQ(pia.readPia(kKeyStateAddr), PIA::kKeyLeft | PIA::kKeyFire);

    // The whole point: independent bits, so move-and-fire is expressible at all.
    EXPECT_TRUE(pia.readPia(kKeyStateAddr) & PIA::kKeyLeft);
    EXPECT_TRUE(pia.readPia(kKeyStateAddr) & PIA::kKeyFire);
    EXPECT_FALSE(pia.readPia(kKeyStateAddr) & PIA::kKeyRight);
}

TEST(PiaKeyStateTest, ReadIsNonDestructive)
{
    // Unlike the keyboard data register, polling must not consume the state --
    // a game reads this every single frame while the key stays down.
    PIA pia;
    pia.setKeyState(PIA::kKeyRight);
    EXPECT_EQ(pia.readPia(kKeyStateAddr), PIA::kKeyRight);
    EXPECT_EQ(pia.readPia(kKeyStateAddr), PIA::kKeyRight);
    EXPECT_EQ(pia.readPia(kKeyStateAddr), PIA::kKeyRight);
}

TEST(PiaKeyStateTest, ClearReleasesEverything)
{
    // Called on focus loss: with no more key-up events coming, anything still
    // marked down would stay down forever.
    PIA pia;
    pia.setKeyState(PIA::kKeyUp | PIA::kKeyButton2);
    pia.clearKeyState();
    EXPECT_EQ(pia.keyState(), 0x00u);
    EXPECT_EQ(pia.readPia(kKeyStateAddr), 0x00u);
}

TEST(PiaKeyStateTest, IsIndependentOfTheKeystrokeBuffer)
{
    // The two input paths coexist: typed text still flows through the FIFO while
    // a game polls held keys, and neither disturbs the other.
    PIA pia;
    pia.setKeyState(PIA::kKeyFire);
    pia.addKeypress('A');

    EXPECT_EQ(pia.readPia(kKeyStateAddr), PIA::kKeyFire);
    EXPECT_TRUE(pia.hasKeypress());
    EXPECT_EQ(pia.getKeypress(), 'A');
    EXPECT_FALSE(pia.hasKeypress());
    // Draining the buffer left the held key alone.
    EXPECT_EQ(pia.readPia(kKeyStateAddr), PIA::kKeyFire);

    pia.clearKeyboardBuffer();
    EXPECT_EQ(pia.readPia(kKeyStateAddr), PIA::kKeyFire);
}
