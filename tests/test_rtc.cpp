// Unit tests for the read-only host-time RTC (Computer::RTC).
//
// The RTC reads the host clock, so to assert exact register values the tests pin
// TZ=UTC and inject a known epoch via setTimeProvider(); the device then converts
// epoch -> localtime (== UTC here) -> BCD, which we compare field by field.

#include "computer/RTC.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>

using Computer::RTC;

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
    EXPECT_FALSE(RTC::isRtcAddress(RTC::kRegFirst - 1));
    EXPECT_TRUE(RTC::isRtcAddress(RTC::kRegLatch));
    EXPECT_TRUE(RTC::isRtcAddress(RTC::kRegDow));
    EXPECT_TRUE(RTC::isRtcAddress(RTC::kRegFatDateHi));
    EXPECT_FALSE(RTC::isRtcAddress(RTC::kRegFatDateHi + 1));
    EXPECT_EQ(RTC::kRegLatch, 0xFE55u);
    EXPECT_EQ(RTC::kRegDow, 0xFE5Cu);
    EXPECT_EQ(RTC::kRegFatDateHi, 0xFE60u);
}

TEST(RtcTest, KnownTimeToBcd)
{
    pinUtc();
    RTC rtc;
    const std::time_t e = utcEpoch(2021, 3, 4, 5, 6, 7); // 2021-03-04 05:06:07
    rtc.setTimeProvider([e] { return e; });              // snapshots immediately

    EXPECT_EQ(rtc.read(RTC::kRegSec), 0x07);
    EXPECT_EQ(rtc.read(RTC::kRegMin), 0x06);
    EXPECT_EQ(rtc.read(RTC::kRegHour), 0x05);
    EXPECT_EQ(rtc.read(RTC::kRegDay), 0x04);
    EXPECT_EQ(rtc.read(RTC::kRegMonth), 0x03);
    EXPECT_EQ(rtc.read(RTC::kRegYear), 0x21); // 2021 -> 21

    // Day of week is a plain 0..6 value (2021-03-04 was a Thursday = 4).
    std::tm *g = std::gmtime(&e);
    EXPECT_EQ(rtc.read(RTC::kRegDow), static_cast<uint8_t>(g->tm_wday));
    EXPECT_EQ(rtc.read(RTC::kRegDow), 4);

    // The latch register itself reads as 0.
    EXPECT_EQ(rtc.read(RTC::kRegLatch), 0x00);
}

TEST(RtcTest, FatFormatRegisters)
{
    pinUtc();
    RTC rtc;
    const std::time_t e = utcEpoch(2021, 3, 4, 5, 6, 7);
    rtc.setTimeProvider([e] { return e; });
    // FAT time = hour<<11 | min<<5 | sec/2 = (5<<11)|(6<<5)|3 = 0x28C3
    EXPECT_EQ(rtc.read(RTC::kRegFatTimeLo), 0xC3);
    EXPECT_EQ(rtc.read(RTC::kRegFatTimeHi), 0x28);
    // FAT date = (2021-1980)<<9 | month<<5 | day = (41<<9)|(3<<5)|4 = 0x5264
    EXPECT_EQ(rtc.read(RTC::kRegFatDateLo), 0x64);
    EXPECT_EQ(rtc.read(RTC::kRegFatDateHi), 0x52);
}

TEST(RtcTest, LatchSnapshotsOnWrite)
{
    pinUtc();
    RTC rtc;
    std::time_t cur = utcEpoch(2021, 3, 4, 5, 6, 7);
    rtc.setTimeProvider([&cur] { return cur; }); // latches the first value
    EXPECT_EQ(rtc.read(RTC::kRegSec), 0x07);

    // Advance the source; without a re-latch the fields stay put.
    cur = utcEpoch(2021, 12, 25, 23, 58, 59);
    EXPECT_EQ(rtc.read(RTC::kRegSec), 0x07) << "fields must not change until re-latched";

    // Writing the latch register snapshots the new time.
    rtc.write(RTC::kRegLatch, 0x00);
    EXPECT_EQ(rtc.read(RTC::kRegSec), 0x59);
    EXPECT_EQ(rtc.read(RTC::kRegMin), 0x58);
    EXPECT_EQ(rtc.read(RTC::kRegHour), 0x23);
    EXPECT_EQ(rtc.read(RTC::kRegDay), 0x25);
    EXPECT_EQ(rtc.read(RTC::kRegMonth), 0x12);
    EXPECT_EQ(rtc.read(RTC::kRegYear), 0x21);
}

TEST(RtcTest, FieldRegistersAreReadOnly)
{
    pinUtc();
    RTC rtc;
    const std::time_t e = utcEpoch(2021, 3, 4, 5, 6, 7);
    rtc.setTimeProvider([e] { return e; });
    rtc.write(RTC::kRegSec, 0x42); // ignored (clock not settable)
    EXPECT_EQ(rtc.read(RTC::kRegSec), 0x07);
}
