/**
 * @file CPU6502.h
 * @brief MOS 6502 Microprocessor Emulator
 * @author 6502 Kernel Project
 */

#ifndef CPU6502_H
#define CPU6502_H

#include <cstdint>
#include <functional>
#include <map>

#include "Memory.h"

namespace Computer {

/**
 * @class CPU6502
 * @brief Complete MOS 6502 microprocessor emulator
 *
 * This class implements a cycle-accurate emulation of the MOS 6502 microprocessor,
 * the heart of many classic computers including the Commodore 64, Apple II, and
 * Nintendo Entertainment System.
 *
 * Features:
 * - Complete 6502 instruction set with all addressing modes
 * - Accurate status flag handling and arithmetic operations
 * - Proper stack operations and interrupt handling
 * - Binary Coded Decimal (BCD) arithmetic support
 * - Cycle counting for timing accuracy
 * - Memory-mapped I/O integration
 *
 * The CPU operates on the classic 6502 architecture with:
 * - 8-bit accumulator and index registers
 * - 16-bit program counter and address bus
 * - 8-bit stack pointer (page 1: $0100-$01FF)
 * - 8-bit processor status register with 6 flags
 *
 * @see Memory, Computer6502
 */
class CPU6502
{
public:
    /**
     * @brief Construct a new CPU6502 instance
     * @param memory Reference to the system memory interface
     */
    explicit CPU6502(Memory &memory);

    /**
     * @struct Registers
     * @brief CPU register set following MOS 6502 specification
     */
    struct Registers
    {
        uint8_t A = 0x00;        ///< Accumulator register
        uint8_t X = 0x00;        ///< X index register
        uint8_t Y = 0x00;        ///< Y index register
        uint16_t PC = 0x0000;    ///< Program Counter (16-bit)
        uint8_t SP = 0xFF;       ///< Stack Pointer (points to page 1: $01xx)
        uint8_t P = 0x20;        ///< Processor Status register (bit 5 always set)
    } reg;

    /**
     * @enum StatusFlags
     * @brief Processor status register flag bits (P register)
     *
     * The 6502 status register contains 8 bits representing various CPU states.
     * Each flag affects instruction behavior and can be set/cleared by operations.
     */
    enum StatusFlags
    {
        kCarry = 0x01,        ///< Carry flag (bit 0) - Set on arithmetic carry/borrow
        kZero = 0x02,         ///< Zero flag (bit 1) - Set when result equals zero
        kInterrupt = 0x04,    ///< Interrupt disable flag (bit 2) - Blocks IRQ when set
        kDecimal = 0x08,      ///< Decimal mode flag (bit 3) - Enables BCD arithmetic
        kBreak = 0x10,        ///< Break flag (bit 4) - Set by BRK instruction
        kUnused = 0x20,       ///< Unused flag (bit 5) - Always set to 1
        kOverflow = 0x40,     ///< Overflow flag (bit 6) - Set on signed arithmetic overflow
        kNegative = 0x80      ///< Negative flag (bit 7) - Set when bit 7 of result is 1
    };

    /**
     * @brief Reset the CPU to initial power-on state
     *
     * Sets all registers to their default values and loads the program counter
     * from the reset vector at memory locations $FFFC-$FFFD.
     */
    void reset();

    /**
     * @brief Set or clear a processor status flag
     * @param flag The status flag to modify
     * @param value true to set the flag, false to clear it
     */
    void setFlag(StatusFlags flag, bool value);

    /**
     * @brief Get the current state of a processor status flag
     * @param flag The status flag to check
     * @return bool true if flag is set, false if clear
     */
    [[nodiscard]] bool getFlag(StatusFlags flag) const;

    /**
     * @brief Update Zero and Negative flags based on a result value
     * @param value The 8-bit result value to test
     * @note Sets Zero flag if value == 0, Negative flag if bit 7 is set
     */
    void updateZeroNegativeFlags(uint8_t value);

    /**
     * @brief Execute one CPU instruction cycle
     * @return bool true if instruction executed successfully, false if unknown opcode
     * @note Fetches opcode from memory at PC, executes instruction, updates cycle count
     */
    bool executeSingleInstruction();

    /**
     * @brief Read next byte from memory and increment program counter
     * @return uint8_t The byte value at current PC location
     */
    uint8_t readByte();

    /**
     * @brief Read next 16-bit word from memory (little-endian) and increment PC
     * @return uint16_t The 16-bit word value in little-endian format
     */
    uint16_t readWord();

    /**
     * @brief Get the current byte at PC without incrementing the program counter
     * @return uint8_t The byte value at current PC location
     */
    [[nodiscard]] uint8_t getCurrentByte() const;

    /**
     * @brief Push a byte value onto the system stack
     * @param value The 8-bit value to push onto stack
     * @note Stack grows downward from $01FF, decrements stack pointer
     */
    void pushByte(uint8_t value);

    /**
     * @brief Pull a byte value from the system stack
     * @return uint8_t The 8-bit value pulled from stack
     * @note Increments stack pointer, stack grows downward from $01FF
     */
    uint8_t pullByte();

    /**
     * @brief Print current CPU register and status information to console
     * @note Used for debugging and development purposes
     */
    void printStatus() const;

    /**
     * @brief Get the total number of CPU cycles executed since power-on
     * @return uint64_t Total cycle count
     * @note Used for timing analysis and performance measurement
     */
    [[nodiscard]] uint64_t getCycles() const;

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
    void handleJmpBase(uint16_t address, uint8_t cycles);
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
    void handlePhx();
    void handlePlx();
    void handlePhy();
    void handlePly();

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
