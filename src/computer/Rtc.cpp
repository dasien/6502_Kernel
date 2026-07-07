/**
 * @file Rtc.cpp
 * @brief Read-only host-time RTC implementation.
 */

#include "computer/Rtc.h"

namespace Computer
{
    namespace
    {
        // Binary value (0-99) to packed BCD.
        uint8_t toBcd(int v)
        {
            return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
        }
    } // namespace

    Rtc::Rtc()
        : now_([] { return std::time(nullptr); })
    {
        latch();
    }

    bool Rtc::isRtcAddress(uint16_t address)
    {
        return address >= kRegFirst && address <= kRegLast;
    }

    void Rtc::latch()
    {
        const std::time_t t = now_();
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        regs_[kRegSec - kRegFirst] = toBcd(local.tm_sec % 60); // clamp leap second
        regs_[kRegMin - kRegFirst] = toBcd(local.tm_min);
        regs_[kRegHour - kRegFirst] = toBcd(local.tm_hour);
        regs_[kRegDay - kRegFirst] = toBcd(local.tm_mday);
        regs_[kRegMonth - kRegFirst] = toBcd(local.tm_mon + 1); // tm_mon is 0-based
        regs_[kRegYear - kRegFirst] = toBcd(local.tm_year % 100); // years since 1900
        regs_[kRegDow - kRegFirst] = static_cast<uint8_t>(local.tm_wday); // 0=Sunday

        // FAT16 directory format (convenience for the 6502 filesystem):
        //   time = [hour:5][min:6][sec/2:5], date = [(year-1980):7][month:4][day:5]
        const int fatYear = (local.tm_year + 1900) - 1980; // tm_year is since 1900
        const uint16_t fatTime = static_cast<uint16_t>(
            (local.tm_hour << 11) | (local.tm_min << 5) | ((local.tm_sec % 60) >> 1));
        const uint16_t fatDate = static_cast<uint16_t>(
            ((fatYear & 0x7F) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
        regs_[kRegFatTimeLo - kRegFirst] = static_cast<uint8_t>(fatTime & 0xFF);
        regs_[kRegFatTimeHi - kRegFirst] = static_cast<uint8_t>(fatTime >> 8);
        regs_[kRegFatDateLo - kRegFirst] = static_cast<uint8_t>(fatDate & 0xFF);
        regs_[kRegFatDateHi - kRegFirst] = static_cast<uint8_t>(fatDate >> 8);
    }

    uint8_t Rtc::read(uint16_t address) const
    {
        if (!isRtcAddress(address))
            return 0;
        if (address == kRegLatch)
            return 0; // latch is write-only
        return regs_[address - kRegFirst];
    }

    void Rtc::write(uint16_t address, uint8_t /*value*/)
    {
        if (address == kRegLatch)
            latch();
        // Field registers are read-only: writes are ignored (clock is not settable).
    }

    void Rtc::setTimeProvider(std::function<std::time_t()> provider)
    {
        now_ = std::move(provider);
        latch();
    }
} // namespace Computer
