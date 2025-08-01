#ifndef CPU6502_H
#define CPU6502_H

#include <cstdint>
#include <functional>
#include <map>

#include "Memory.h"

namespace Computer {

class CPU6502
{
public:
    explicit CPU6502(Memory &memory);

    struct Registers
    {
        uint8_t A = 0x00; // Accumulator
        uint8_t X = 0x00; // X Index Register
        uint8_t Y = 0x00; // Y Index Register
        uint16_t PC = 0x0000; // Program Counter
        uint8_t SP = 0xFF; // Stack Pointer
        uint8_t P = 0x20; // Processor Status (bit 5 always 1)
    } reg;

    // Status flags
    enum StatusFlags
    {
        kCarry = 0x01,
        kZero = 0x02,
        kInterrupt = 0x04,
        kDecimal = 0x08,
        kBreak = 0x10,
        kUnused = 0x20,
        kOverflow = 0x40,
        kNegative = 0x80
    };

    void reset();

    void setFlag(StatusFlags flag, bool value);

    bool getFlag(StatusFlags flag) const;

    void updateZeroNegativeFlags(uint8_t value);

    uint8_t readByte();

    uint16_t readWord();

    void pushByte(uint8_t value);

    uint8_t pullByte();

    bool executeSingleInstruction();

    void printStatus() const;

    uint64_t getCycles() const;
    
    uint8_t getCurrentByte() const;

private:
    Memory &mem_;
    uint64_t cycles_;
    using handlerFunction = std::function<void()>;
    std::map<uint8_t, handlerFunction> handlers_;

    void initializeInstructionHandlers();

    // Addressing calculation functions
    std::pair<uint16_t, bool> calculateAddress(bool use_one_byte, uint8_t offset);
    uint16_t calculateAddressSimple(bool use_one_byte, uint8_t offset);
    std::pair<uint16_t, bool> calculateRelativeAddress(uint8_t offset);
    std::pair<uint16_t, uint8_t> calculateIndexedAddress(uint8_t offset);
    std::pair<uint16_t, uint8_t> calculateIndirectAddress(uint8_t offset);
    bool checkPageBoundaryCrossed(uint16_t base_addr, uint16_t final_addr);
    bool validateAddress(uint16_t address);

    // ALU helper functions
    uint8_t addValues(uint8_t val1, uint8_t val2);
    uint8_t subtractValues(uint8_t val1, uint8_t val2);
    void compareValues(uint8_t val1, uint8_t val2);
    uint8_t convertToBcd(uint8_t value);

    // Stack helper functions
    void pushStack16(uint16_t value);
    uint16_t popStack16();

    /**********************************
    ** Assembly Instruction Handlers **
    **********************************/

    // Math instructions (Add/Subtract).
    void handleAdcImmediate();
    void handleAdcZeroPage();
    void handleAdcZeroPageX();
    void handleAdcAbsolute();
    void handleAdcAbsoluteX();
    void handleAdcAbsoluteY();
    void handleAdcIndexedIndirect();
    void handleAdcIndirectIndexed();
    void handleAdcBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleSbcImmediate();
    void handleSbcZeroPage();
    void handleSbcZeroPageX();
    void handleSbcAbsolute();
    void handleSbcAbsoluteX();
    void handleSbcAbsoluteY();
    void handleSbcIndexedIndirect();
    void handleSbcIndirectIndexed();
    void handleSbcBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);

    // Bitwise operations.
    void handleAndImmediate();
    void handleAndZeroPage();
    void handleAndZeroPageX();
    void handleAndAbsolute();
    void handleAndAbsoluteX();
    void handleAndAbsoluteY();
    void handleAndIndexedIndirect();
    void handleAndIndirectIndexed();
    void handleAndBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleEorImmediate();
    void handleEorZeroPage();
    void handleEorZeroPageX();
    void handleEorAbsolute();
    void handleEorAbsoluteX();
    void handleEorAbsoluteY();
    void handleEorIndexedIndirect();
    void handleEorIndirectIndexed();
    void handleEorBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleOraImmediate();
    void handleOraZeroPage();
    void handleOraZeroPageX();
    void handleOraAbsolute();
    void handleOraAbsoluteX();
    void handleOraAbsoluteY();
    void handleOraIndexedIndirect();
    void handleOraIndirectIndexed();
    void handleOraBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);

    // Shift & rotation (Left/Right) operations.
    void handleAslAccumulator();
    void handleAslZeroPage();
    void handleAslZeroPageX();
    void handleAslAbsolute();
    void handleAslAbsoluteX();
    void handleAslBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleLsrAccumulator();
    void handleLsrZeroPage();
    void handleLsrZeroPageX();
    void handleLsrAbsolute();
    void handleLsrAbsoluteX();
    void handleLsrBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleRolAccumulator();
    void handleRolZeroPage();
    void handleRolZeroPageX();
    void handleRolAbsolute();
    void handleRolAbsoluteX();
    void handleRolBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleRorAccumulator();
    void handleRorZeroPage();
    void handleRorZeroPageX();
    void handleRorAbsolute();
    void handleRorAbsoluteX();
    void handleRorBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);

    // Branching Instructions.
    void handleBcc();
    void handleBcs();
    void handleBeq();
    void handleBmi();
    void handleBne();
    void handleBpl();
    void handleBvc();
    void handleBvs();

    // Bit testing.
    void handleBitZeroPage();
    void handleBitAbsolute();
    void handleBitBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);

    // Flag Instructions.
    void handleClc();
    void handleCld();
    void handleCli();
    void handleClv();
    void handleSec();
    void handleSed();
    void handleSei();

    // Compare Instructions (A/X/Y).
    void handleCmpImmediate();
    void handleCmpZeroPage();
    void handleCmpZeroPageX();
    void handleCmpAbsolute();
    void handleCmpAbsoluteX();
    void handleCmpAbsoluteY();
    void handleCmpIndexedIndirect();
    void handleCmpIndirectIndexed();
    void handleCmpBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleCpxImmediate();
    void handleCpxZeroPage();
    void handleCpxAbsolute();
    void handleCpxBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleCpyImmediate();
    void handleCpyZeroPage();
    void handleCpyAbsolute();
    void handleCpyBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);

    // Incrementors & Decrementors (Memory/X/Y).
    void handleIncZeroPage();\
    void handleIncZeroPageX();
    void handleIncAbsolute();
    void handleIncAbsoluteX();
    void handleIncBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleInx();
    void handleIny();
    void handleDecZeroPage();
    void handleDecZeroPageX();
    void handleDecAbsolute();
    void handleDecAbsoluteX();
    void handleDecBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleDex();
    void handleDey();

    // Jump & Return operations.
    void handleJmpAbsolute();
    void handleJmpIndirect();
    void handleJmpBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleJsr();
    void handleRti();
    void handleRts();

    // Load Value (A/X/Y).
    void handleLdaImmediate();
    void handleLdaZeroPage();
    void handleLdaZeroPageX();
    void handleLdaAbsolute();
    void handleLdaAbsoluteX();
    void handleLdaAbsoluteY();
    void handleLdaIndexedIndirect();
    void handleLdaIndirectIndexed();
    void handleLdaBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleLdxImmediate();
    void handleLdxZeroPage();
    void handleLdxZeroPageY();
    void handleLdxAbsolute();
    void handleLdxAbsoluteY();
    void handleLdxBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleLdyImmediate();
    void handleLdyZeroPage();
    void handleLdyZeroPageX();
    void handleLdyAbsolute();
    void handleLdyAbsoluteX();
    void handleLdyBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);

    // Store value (A/X/Y)
    void handleStaZeroPage();
    void handleStaZeroPageX();
    void handleStaAbsolute();
    void handleStaAbsoluteX();
    void handleStaAbsoluteY();
    void handleStaIndexedIndirect();
    void handleStaIndirectIndexed();
    void handleStaBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleStxZeroPage();
    void handleStxZeroPageY();
    void handleStxAbsolute();
    void handleStxBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);
    void handleStyZeroPage();
    void handleStyZeroPageX();
    void handleStyAbsolute();
    void handleStyBase(uint16_t address, uint8_t pc_offset, uint8_t cycles);

    // Stack instructions.
    void handlePha();
    void handlePhp();
    void handlePla();
    void handlePlp();

    // Register Transfers (A/X/Y/Stack).
    void handleTax();
    void handleTay();
    void handleTsx();
    void handleTxa();
    void handleTxs();
    void handleTya();

    // No operation & break.
    void handleNop();
    void handleBrk();
};

} // namespace Computer

#endif // CPU6502_H
