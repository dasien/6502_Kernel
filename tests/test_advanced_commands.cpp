/**
 * @file test_advanced_commands.cpp
 * @brief Tests for advanced monitor commands F:, M:, X:
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <algorithm>

// Mock command parser for testing command syntax
class CommandParser {
public:
    struct ParseResult {
        bool success;
        std::string error;
        uint16_t startAddr;
        uint16_t endAddr;
        uint8_t value;
        uint16_t destAddr;
        uint8_t mode;
        std::vector<uint8_t> pattern;
    };

    static ParseResult parseFillCommand(const std::string& cmd) {
        ParseResult result = {false, "", 0, 0, 0, 0, 0, {}};

        // Expected format: "F:XXXX-YYYY,ZZ"
        if (cmd.length() < 11 || cmd[0] != 'F' || cmd[1] != ':') {
            result.error = "Invalid F: command syntax";
            return result;
        }

        // Find dash and comma
        size_t dashPos = cmd.find('-');
        size_t commaPos = cmd.find(',');

        if (dashPos == std::string::npos || commaPos == std::string::npos) {
            result.error = "Missing dash or comma";
            return result;
        }

        // Parse addresses and value
        std::string startStr = cmd.substr(2, dashPos - 2);
        std::string endStr = cmd.substr(dashPos + 1, commaPos - dashPos - 1);
        std::string valueStr = cmd.substr(commaPos + 1);

        if (startStr.length() != 4 || endStr.length() != 4 || valueStr.length() != 2) {
            result.error = "Invalid address or value format";
            return result;
        }

        // Convert hex strings (simplified)
        result.startAddr = hexStringToUint16(startStr);
        result.endAddr = hexStringToUint16(endStr);
        result.value = hexStringToUint8(valueStr);

        if (result.startAddr > result.endAddr) {
            result.error = "Start address > end address";
            return result;
        }

        result.success = true;
        return result;
    }

    static ParseResult parseMoveCommand(const std::string& cmd) {
        ParseResult result = {false, "", 0, 0, 0, 0, 0, {}};

        // Expected format: "M:XXXX-YYYY,ZZZZ,B"
        if (cmd.length() < 15 || cmd[0] != 'M' || cmd[1] != ':') {
            result.error = "Invalid M: command syntax";
            return result;
        }

        // Find separators
        size_t dashPos = cmd.find('-');
        size_t comma1Pos = cmd.find(',');
        size_t comma2Pos = cmd.find(',', comma1Pos + 1);

        if (dashPos == std::string::npos || comma1Pos == std::string::npos || comma2Pos == std::string::npos) {
            result.error = "Missing separators";
            return result;
        }

        // Parse components
        std::string startStr = cmd.substr(2, dashPos - 2);
        std::string endStr = cmd.substr(dashPos + 1, comma1Pos - dashPos - 1);
        std::string destStr = cmd.substr(comma1Pos + 1, comma2Pos - comma1Pos - 1);
        std::string modeStr = cmd.substr(comma2Pos + 1);

        if (startStr.length() != 4 || endStr.length() != 4 || destStr.length() != 4 || modeStr.length() != 1) {
            result.error = "Invalid format";
            return result;
        }

        result.startAddr = hexStringToUint16(startStr);
        result.endAddr = hexStringToUint16(endStr);
        result.destAddr = hexStringToUint16(destStr);
        result.mode = modeStr[0] - '0';

        if (result.startAddr > result.endAddr) {
            result.error = "Start address > end address";
            return result;
        }

        if (result.mode > 1) {
            result.error = "Mode must be 0 or 1";
            return result;
        }

        result.success = true;
        return result;
    }

    static ParseResult parseSearchCommand(const std::string& cmd) {
        ParseResult result = {false, "", 0, 0, 0, 0, 0, {}};

        // Expected format: "X:XXXX-YYYY,PATTERN"
        if (cmd.length() < 12 || cmd[0] != 'X' || cmd[1] != ':') {
            result.error = "Invalid X: command syntax";
            return result;
        }

        // Find separators
        size_t dashPos = cmd.find('-');
        size_t commaPos = cmd.find(',');

        if (dashPos == std::string::npos || commaPos == std::string::npos) {
            result.error = "Missing dash or comma";
            return result;
        }

        // Parse addresses
        std::string startStr = cmd.substr(2, dashPos - 2);
        std::string endStr = cmd.substr(dashPos + 1, commaPos - dashPos - 1);
        std::string patternStr = cmd.substr(commaPos + 1);

        if (startStr.length() != 4 || endStr.length() != 4) {
            result.error = "Invalid address format";
            return result;
        }

        result.startAddr = hexStringToUint16(startStr);
        result.endAddr = hexStringToUint16(endStr);

        // Parse pattern (space-separated hex bytes)
        std::vector<std::string> patternBytes = splitString(patternStr, ' ');
        if (patternBytes.empty() || patternBytes.size() > 16) {
            result.error = "Pattern must be 1-16 bytes";
            return result;
        }

        for (const std::string& byteStr : patternBytes) {
            if (byteStr.length() != 2) {
                result.error = "Each pattern byte must be 2 hex digits";
                return result;
            }
            result.pattern.push_back(hexStringToUint8(byteStr));
        }

        if (result.startAddr > result.endAddr) {
            result.error = "Start address > end address";
            return result;
        }

        result.success = true;
        return result;
    }

private:
    static uint16_t hexStringToUint16(const std::string& str) {
        // Simplified hex conversion
        uint16_t result = 0;
        for (char c : str) {
            result <<= 4;
            if (c >= '0' && c <= '9') result |= (c - '0');
            else if (c >= 'A' && c <= 'F') result |= (c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') result |= (c - 'a' + 10);
        }
        return result;
    }

    static uint8_t hexStringToUint8(const std::string& str) {
        return (uint8_t)hexStringToUint16(str);
    }

    static std::vector<std::string> splitString(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        size_t start = 0;
        size_t end = str.find(delimiter);

        while (end != std::string::npos) {
            result.push_back(str.substr(start, end - start));
            start = end + 1;
            end = str.find(delimiter, start);
        }
        result.push_back(str.substr(start));
        return result;
    }
};

class AdvancedCommandTest : public ::testing::Test {
protected:
    CommandParser parser;
};

/**
 * Test F: Fill Command Parsing
 */
TEST_F(AdvancedCommandTest, FillCommandParsing) {
    // Valid fill commands
    auto result = CommandParser::parseFillCommand("F:8000-8FFF,AA");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(0x8000, result.startAddr);
    EXPECT_EQ(0x8FFF, result.endAddr);
    EXPECT_EQ(0xAA, result.value);

    result = CommandParser::parseFillCommand("F:0000-FFFF,00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(0x0000, result.startAddr);
    EXPECT_EQ(0xFFFF, result.endAddr);
    EXPECT_EQ(0x00, result.value);

    // Invalid fill commands
    result = CommandParser::parseFillCommand("F:8000-7FFF,AA");
    EXPECT_FALSE(result.success);
    EXPECT_NE("", result.error);

    result = CommandParser::parseFillCommand("F:8000,AA");
    EXPECT_FALSE(result.success);

    result = CommandParser::parseFillCommand("G:8000-8FFF,AA");
    EXPECT_FALSE(result.success);
}

/**
 * Test M: Move/Copy Command Parsing
 */
TEST_F(AdvancedCommandTest, MoveCommandParsing) {
    // Valid move/copy commands
    auto result = CommandParser::parseMoveCommand("M:8000-8FFF,9000,0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(0x8000, result.startAddr);
    EXPECT_EQ(0x8FFF, result.endAddr);
    EXPECT_EQ(0x9000, result.destAddr);
    EXPECT_EQ(0, result.mode);

    result = CommandParser::parseMoveCommand("M:1000-10FF,2000,1");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(0x1000, result.startAddr);
    EXPECT_EQ(0x10FF, result.endAddr);
    EXPECT_EQ(0x2000, result.destAddr);
    EXPECT_EQ(1, result.mode);

    // Invalid move/copy commands
    result = CommandParser::parseMoveCommand("M:8FFF-8000,9000,0");
    EXPECT_FALSE(result.success);

    result = CommandParser::parseMoveCommand("M:8000-8FFF,9000,2");
    EXPECT_FALSE(result.success);

    result = CommandParser::parseMoveCommand("M:8000-8FFF,9000");
    EXPECT_FALSE(result.success);
}

/**
 * Test X: Search Command Parsing
 */
TEST_F(AdvancedCommandTest, SearchCommandParsing) {
    // Valid search commands
    auto result = CommandParser::parseSearchCommand("X:8000-8FFF,4C");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(0x8000, result.startAddr);
    EXPECT_EQ(0x8FFF, result.endAddr);
    EXPECT_EQ(1, result.pattern.size());
    EXPECT_EQ(0x4C, result.pattern[0]);

    result = CommandParser::parseSearchCommand("X:0000-FFFF,A9 20 4C");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(0x0000, result.startAddr);
    EXPECT_EQ(0xFFFF, result.endAddr);
    EXPECT_EQ(3, result.pattern.size());
    EXPECT_EQ(0xA9, result.pattern[0]);
    EXPECT_EQ(0x20, result.pattern[1]);
    EXPECT_EQ(0x4C, result.pattern[2]);

    // Invalid search commands
    result = CommandParser::parseSearchCommand("X:8FFF-8000,4C");
    EXPECT_FALSE(result.success);

    result = CommandParser::parseSearchCommand("X:8000-8FFF,");
    EXPECT_FALSE(result.success);

    result = CommandParser::parseSearchCommand("X:8000-8FFF,4");
    EXPECT_FALSE(result.success);

    // Pattern too long (>16 bytes)
    result = CommandParser::parseSearchCommand("X:8000-8FFF,01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11");
    EXPECT_FALSE(result.success);
}

/**
 * Test command syntax variations and edge cases
 */
TEST_F(AdvancedCommandTest, CommandSyntaxVariations) {
    // Test different case combinations
    auto result = CommandParser::parseFillCommand("F:8000-8fff,aa");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(0x8000, result.startAddr);
    EXPECT_EQ(0x8FFF, result.endAddr);
    EXPECT_EQ(0xAA, result.value);

    // Test single byte ranges
    result = CommandParser::parseFillCommand("F:8000-8000,FF");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(0x8000, result.startAddr);
    EXPECT_EQ(0x8000, result.endAddr);

    // Test memory boundaries
    result = CommandParser::parseFillCommand("F:0000-0000,00");
    EXPECT_TRUE(result.success);

    result = CommandParser::parseFillCommand("F:FFFF-FFFF,FF");
    EXPECT_TRUE(result.success);
}

/**
 * Test error message quality
 */
TEST_F(AdvancedCommandTest, ErrorMessages) {
    auto result = CommandParser::parseFillCommand("F:8FFF-8000,AA");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(std::string::npos, result.error.find("address"));

    result = CommandParser::parseMoveCommand("M:8000-8FFF,9000,5");
    EXPECT_FALSE(result.success);
    EXPECT_NE(std::string::npos, result.error.find("Mode"));

    result = CommandParser::parseSearchCommand("X:8000-8FFF,GG");
    EXPECT_FALSE(result.success);
    EXPECT_NE(std::string::npos, result.error.find("hex"));
}