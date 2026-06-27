/**
 * @file test_acia_xmodem.cpp
 * @brief Headless end-to-end spike: the emulated 6551 ACIA + the ported
 *        Daryl Rictor XMODEM/CRC receiver.
 *
 * The C++ side plays the "other end of the wire" (an XMODEM sender): it feeds
 * framed blocks into the ACIA's RX FIFO and reads the receiver's ACK/NAK and
 * the start-of-transfer 'C' from the TX FIFO. The 6502 runs the real ported
 * XMODEM receiver blob loaded into RAM at $2000. Success = the payload lands in
 * RAM at the load address embedded in block 1.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <vector>

#include "computer/Computer6502.h"
#include "computer/Acia.h"

namespace
{
    constexpr uint8_t SOH = 0x01;
    constexpr uint8_t EOT = 0x04;
    constexpr uint8_t ACK = 0x06;
    constexpr uint8_t XCRC = 'C'; // receiver's CRC-mode start byte

    // CRC-16/XMODEM (poly 0x1021, init 0x0000) over n bytes - matches the
    // table-driven CalcCRC in the ported receiver.
    uint16_t crc16_xmodem(const uint8_t *d, size_t n)
    {
        uint16_t c = 0;
        for (size_t i = 0; i < n; ++i)
        {
            c ^= static_cast<uint16_t>(d[i]) << 8;
            for (int b = 0; b < 8; ++b)
                c = (c & 0x8000) ? static_cast<uint16_t>((c << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(c << 1);
        }
        return c;
    }

    // Build the 133-byte on-the-wire frame: SOH, blk, ~blk, 128 data, crc-hi, crc-lo.
    std::vector<uint8_t> makeFrame(uint8_t blk, const uint8_t data128[128])
    {
        std::vector<uint8_t> f;
        f.push_back(SOH);
        f.push_back(blk);
        f.push_back(static_cast<uint8_t>(~blk));
        f.insert(f.end(), data128, data128 + 128);
        const uint16_t crc = crc16_xmodem(data128, 128);
        f.push_back(static_cast<uint8_t>(crc >> 8));
        f.push_back(static_cast<uint8_t>(crc & 0xFF));
        return f;
    }
}

TEST(AciaXmodem, ReceivesOneBlockToLoadAddress)
{
    Computer::Computer6502 computer;
    computer.power_on();
    auto *mem = computer.getMemory();
    auto *cpu = computer.getCpu();
    auto *acia = computer.getAcia();

    // Load the XMODEM receiver blob at $2000 (built by the xmodem_bin target).
    std::ifstream f("../kernel/xmodem.bin", std::ios::binary);
    ASSERT_TRUE(f.good()) << "xmodem.bin not found - build the xmodem_bin target";
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    ASSERT_GE(blob.size(), 0x300u);
    ASSERT_EQ(blob[0], 0x4Cu); // JMP (entry to XModemRcv)
    for (size_t i = 0; i < blob.size(); ++i)
        mem->write(static_cast<uint16_t>(0x2000 + i), blob[i]);

    // Payload to "send", and the load address it should land at.
    const uint16_t loadAddr = 0x5000;
    std::vector<uint8_t> payload;
    for (int i = 0; i < 80; ++i)
        payload.push_back(static_cast<uint8_t>(0x41 + (i % 26))); // A..Z repeating

    // Block 1 data: [addr-lo, addr-hi, payload..., zero pad to 128].
    uint8_t data[128] = {0};
    data[0] = static_cast<uint8_t>(loadAddr & 0xFF);
    data[1] = static_cast<uint8_t>(loadAddr >> 8);
    for (size_t i = 0; i < payload.size(); ++i)
        data[2 + i] = payload[i];
    const std::vector<uint8_t> frame = makeFrame(0x01, data);

    // Enter XModemRcv via JSR semantics: push a $FFFF return so its final RTS
    // leaves the blob, then start at the $2000 entry (jmp XModemRcv).
    cpu->reg.SP = 0xFF;
    cpu->pushByte(0xFF);
    cpu->pushByte(0xFF);
    cpu->reg.PC = 0x2000;

    bool sentFrame = false;
    bool sentEot = false;
    bool exited = false;
    for (int i = 0; i < 20'000'000; ++i)
    {
        const uint16_t pc = cpu->reg.PC;
        if (pc < 0x2000 || pc > 0x2FFF) // RTS'd to the sentinel -> done
        {
            exited = true;
            break;
        }
        if (!cpu->executeSingleInstruction())
            break;
        // Play the sender: respond to what the receiver transmits.
        while (acia->hostHasTx())
        {
            const uint8_t t = acia->hostRecv();
            if (!sentFrame && t == XCRC)
            {
                for (uint8_t b : frame)
                    acia->hostSend(b);
                sentFrame = true;
            }
            else if (sentFrame && !sentEot && t == ACK)
            {
                acia->hostSend(EOT); // one block sent; end the transfer
                sentEot = true;
            }
        }
    }

    EXPECT_TRUE(sentFrame) << "receiver never sent 'C' to start";
    EXPECT_TRUE(sentEot) << "receiver never ACKed the block";
    EXPECT_TRUE(exited) << "receiver did not return (RTS) after EOT";

    for (size_t i = 0; i < payload.size(); ++i)
        EXPECT_EQ(mem->read(static_cast<uint16_t>(loadAddr + i)), payload[i])
            << "payload mismatch at offset " << i;
}
