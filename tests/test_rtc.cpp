// Unit tests for the read-only host-time RTC (Computer::Rtc).
//
// The RTC reads the host clock, so to assert exact register values the tests pin
// TZ=UTC and inject a known epoch via setTimeProvider(); the device then converts
// epoch -> localtime (== UTC here) -> BCD, which we compare field by field.

#include "computer/Rtc.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>

using Computer::Rtc;

namespace
{
    // Build a UTC epoch from Y/M/D H:M:S (timegm ignores TZ).
    std::time_t utcEpoch(int y, int mon, int d, int h, int mi, int s)
    {
        std::tm t{};
        t.tm_year = y - 1900;
        t.tm_mon = mon - 1;
        t.tm_mday = d;
        t.tm_hour = h;
        t.tm_min = mi;
        t.tm_sec = s;
        return timegm(&t);
    }

    void pinUtc()
    {
        setenv("TZ", "UTC", 1);
        tzset();
    }
} // namespace

TEST(RtcTest, AddressRange)
{
    EXPECT_FALSE(Rtc::isRtcAddress(Rtc::kRegFirst - 1));
    EXPECT_TRUE(Rtc::isRtcAddress(Rtc::kRegLatch));
    EXPECT_TRUE(Rtc::isRtcAddress(Rtc::kRegDow));
    EXPECT_FALSE(Rtc::isRtcAddress(Rtc::kRegDow + 1));
    EXPECT_EQ(Rtc::kRegLatch, 0xFE55u);
    EXPECT_EQ(Rtc::kRegDow, 0xFE5Cu);
}

TEST(RtcTest, KnownTimeToBcd)
{
    pinUtc();
    Rtc rtc;
    const std::time_t e = utcEpoch(2021, 3, 4, 5, 6, 7); // 2021-03-04 05:06:07
    rtc.setTimeProvider([e] { return e; });              // snapshots immediately

    EXPECT_EQ(rtc.read(Rtc::kRegSec), 0x07);
    EXPECT_EQ(rtc.read(Rtc::kRegMin), 0x06);
    EXPECT_EQ(rtc.read(Rtc::kRegHour), 0x05);
    EXPECT_EQ(rtc.read(Rtc::kRegDay), 0x04);
    EXPECT_EQ(rtc.read(Rtc::kRegMonth), 0x03);
    EXPECT_EQ(rtc.read(Rtc::kRegYear), 0x21); // 2021 -> 21

    // Day of week is a plain 0..6 value (2021-03-04 was a Thursday = 4).
    std::tm *g = std::gmtime(&e);
    EXPECT_EQ(rtc.read(Rtc::kRegDow), static_cast<uint8_t>(g->tm_wday));
    EXPECT_EQ(rtc.read(Rtc::kRegDow), 4);

    // The latch register itself reads as 0.
    EXPECT_EQ(rtc.read(Rtc::kRegLatch), 0x00);
}

TEST(RtcTest, LatchSnapshotsOnWrite)
{
    pinUtc();
    Rtc rtc;
    std::time_t cur = utcEpoch(2021, 3, 4, 5, 6, 7);
    rtc.setTimeProvider([&cur] { return cur; }); // latches the first value
    EXPECT_EQ(rtc.read(Rtc::kRegSec), 0x07);

    // Advance the source; without a re-latch the fields stay put.
    cur = utcEpoch(2021, 12, 25, 23, 58, 59);
    EXPECT_EQ(rtc.read(Rtc::kRegSec), 0x07) << "fields must not change until re-latched";

    // Writing the latch register snapshots the new time.
    rtc.write(Rtc::kRegLatch, 0x00);
    EXPECT_EQ(rtc.read(Rtc::kRegSec), 0x59);
    EXPECT_EQ(rtc.read(Rtc::kRegMin), 0x58);
    EXPECT_EQ(rtc.read(Rtc::kRegHour), 0x23);
    EXPECT_EQ(rtc.read(Rtc::kRegDay), 0x25);
    EXPECT_EQ(rtc.read(Rtc::kRegMonth), 0x12);
    EXPECT_EQ(rtc.read(Rtc::kRegYear), 0x21);
}

TEST(RtcTest, FieldRegistersAreReadOnly)
{
    pinUtc();
    Rtc rtc;
    const std::time_t e = utcEpoch(2021, 3, 4, 5, 6, 7);
    rtc.setTimeProvider([e] { return e; });
    rtc.write(Rtc::kRegSec, 0x42); // ignored (clock not settable)
    EXPECT_EQ(rtc.read(Rtc::kRegSec), 0x07);
}
