/**
 * @file Acia.h
 * @brief Emulated 6551 ACIA (serial UART) with host-side RX/TX FIFOs.
 * @author 6502 Kernel Project
 */

#ifndef ACIA_H
#define ACIA_H

#include <cstdint>
#include <deque>

namespace Computer
{
    /**
     * @class Acia
     * @brief A minimal 6551-style serial port exposed through four I/O registers,
     *        just past the block device ($FE29-$FE2C).
     *
     * | Addr   | Name         | Read                         | Write              |
     * |--------|--------------|------------------------------|--------------------|
     * | $FE29  | ACIA_DATA    | next received byte (pops RX) | queue a byte to TX |
     * | $FE2A  | ACIA_STATUS  | status bits (see below)      | (programmed reset) |
     * | $FE2B  | ACIA_COMMAND | last command written         | store command      |
     * | $FE2C  | ACIA_CONTROL | last control written         | store control      |
     *
     * Status bits follow the real 6551 well enough for polled drivers:
     *   bit 3 ($08) = receiver full  (a byte is waiting to be read)
     *   bit 4 ($10) = transmitter empty (always set here: TX is instant)
     *
     * The 6502 side is the usual polled driver (e.g. the bundled XMODEM):
     *   send: wait for TX-empty, write ACIA_DATA.
     *   recv: test RX-full, read ACIA_DATA.
     *
     * The "other end of the wire" is driven from the host via hostSend()/hostRecv():
     * hostSend() pushes bytes the 6502 will receive; hostRecv() pops bytes the 6502
     * transmitted. For the spike these are driven directly by a test; a real serial
     * endpoint (socket/PTY) would pump the same FIFOs from a background thread.
     *
     * @see Memory, Computer6502, BlockDevice
     */
    class Acia
    {
    public:
        /// Register addresses in the always-mapped I/O page.
        static constexpr uint16_t kRegData = 0xFE29;
        static constexpr uint16_t kRegStatus = 0xFE2A;
        static constexpr uint16_t kRegCommand = 0xFE2B;
        static constexpr uint16_t kRegControl = 0xFE2C;

        /// Status register bits (6551-compatible subset).
        static constexpr uint8_t kStatusRxFull = 0x08;   ///< a received byte is waiting
        static constexpr uint8_t kStatusTxEmpty = 0x10;  ///< transmitter ready

        /// @brief Whether an address falls within the ACIA registers ($FE29-$FE2C).
        [[nodiscard]] static bool isAciaAddress(uint16_t address);

        /**
         * @brief Read an ACIA register.
         * @note Non-const: reading ACIA_DATA pops a byte from the RX FIFO.
         */
        uint8_t read(uint16_t address);

        /// @brief Write an ACIA register (ACIA_DATA queues a byte for transmission).
        void write(uint16_t address, uint8_t value);

        // ---- host ("other end of the wire") interface ----

        /// @brief Queue a byte for the 6502 to receive.
        void hostSend(uint8_t byte);

        /// @brief True if the 6502 has transmitted a byte the host hasn't taken.
        [[nodiscard]] bool hostHasTx() const { return !tx_.empty(); }

        /// @brief Number of bytes the 6502 has transmitted and not yet taken.
        [[nodiscard]] size_t hostTxCount() const { return tx_.size(); }

        /// @brief Pop the next byte the 6502 transmitted (0 if none).
        uint8_t hostRecv();

    private:
        std::deque<uint8_t> rx_;   ///< host -> 6502 (bytes awaiting ACIA_DATA reads)
        std::deque<uint8_t> tx_;   ///< 6502 -> host (bytes written to ACIA_DATA)
        uint8_t command_ = 0;      ///< last ACIA_COMMAND written
        uint8_t control_ = 0;      ///< last ACIA_CONTROL written
    };
} // namespace Computer

#endif // ACIA_H
