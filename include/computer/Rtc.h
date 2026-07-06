/**
 * @file Rtc.h
 * @brief Read-only real-time clock that mirrors the host wall-clock time.
 * @author 6502 Kernel Project
 */

#ifndef RTC_H
#define RTC_H

#include <cstdint>
#include <array>
#include <ctime>
#include <functional>

namespace Computer
{
    /**
     * @class Rtc
     * @brief A simple read-only RTC exposed as memory-mapped registers in the
     *        always-mapped I/O page ($FE55-$FE5C), just past the SID.
     *
     * The clock is not settable: it reflects the host's current local time, so it
     * is always correct and needs no battery-backed persistence. The 6502 reads
     * the date/time as BCD field registers. Writing the latch register snapshots
     * the host time into the fields, so a multi-register read can't straddle a
     * second boundary (the same reason real RTCs have a latch).
     *
     * | Addr   | Register    | Notes                                     |
     * |--------|-------------|-------------------------------------------|
     * | $FE55  | RTC_LATCH   | write: snapshot host time; read: 0        |
     * | $FE56  | RTC_SEC     | seconds, BCD 00-59                        |
     * | $FE57  | RTC_MIN     | minutes, BCD 00-59                        |
     * | $FE58  | RTC_HOUR    | hours, BCD 00-23 (24-hour)                |
     * | $FE59  | RTC_DAY     | day of month, BCD 01-31                   |
     * | $FE5A  | RTC_MONTH   | month, BCD 01-12                          |
     * | $FE5B  | RTC_YEAR    | year mod 100, BCD 00-99 (add 2000)        |
     * | $FE5C  | RTC_DOW     | day of week, 0=Sunday .. 6=Saturday       |
     *
     * The time source is injectable (defaults to the real clock) so tests can
     * pin a known timestamp and assert exact register values.
     *
     * @see Memory, Computer6502, Sid, Acia
     */
    class Rtc
    {
    public:
        static constexpr uint16_t kRegLatch = 0xFE55;
        static constexpr uint16_t kRegSec = 0xFE56;
        static constexpr uint16_t kRegMin = 0xFE57;
        static constexpr uint16_t kRegHour = 0xFE58;
        static constexpr uint16_t kRegDay = 0xFE59;
        static constexpr uint16_t kRegMonth = 0xFE5A;
        static constexpr uint16_t kRegYear = 0xFE5B;
        static constexpr uint16_t kRegDow = 0xFE5C;

        static constexpr uint16_t kRegFirst = kRegLatch;
        static constexpr uint16_t kRegLast = kRegDow;

        /// Construct with the default (real) time source and take an initial
        /// snapshot so reads are valid even before the first latch.
        Rtc();

        [[nodiscard]] static bool isRtcAddress(uint16_t address);

        [[nodiscard]] uint8_t read(uint16_t address) const;
        void write(uint16_t address, uint8_t value);

        /// Override the time source (for tests). Re-snapshots immediately.
        void setTimeProvider(std::function<std::time_t()> provider);

        /// Snapshot the current time into the field registers (as RTC_LATCH does).
        void latch();

    private:
        std::function<std::time_t()> now_;         ///< time source (real clock by default)
        std::array<uint8_t, kRegLast - kRegFirst + 1> regs_{}; ///< latched BCD fields
    };
} // namespace Computer

#endif // RTC_H
