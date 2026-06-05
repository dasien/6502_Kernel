#include "CPU6502.h"

namespace Computer {

CPU6502::CPU6502(Memory &memory) : mem_(memory), cycles_(0)
{
    reset();
    initializeInstructionHandlers();
}

void CPU6502::reset()
{
    reg.A = 0x00;
    reg.X = 0x00;
    reg.Y = 0x00;
    reg.SP = 0xFF;
    reg.P = 0x20 | kInterrupt; // Set interrupt disable flag

    // Load reset vector from $FFFC/$FFFD
    reg.PC = mem_.readWord(0xFFFC);
    cycles_ = 0;
}

void CPU6502::setFlag(const StatusFlags flag, const bool value)
{
    if (value)
    {
        reg.P |= flag;
    } else
    {
        reg.P &= ~flag;
    }
}

bool CPU6502::getFlag(const StatusFlags flag) const
{
    return (reg.P & flag) != 0;
}

void CPU6502::updateZeroNegativeFlags(const uint8_t value)
{
    setFlag(kZero, value == 0);
    setFlag(kNegative, (value & 0x80) != 0);
}

uint8_t CPU6502::readByte()
{
    cycles_++;
    return mem_.read(reg.PC++);
}

uint16_t CPU6502::readWord()
{
    const uint16_t word = mem_.readWord(reg.PC);
    reg.PC += 2;
    cycles_ += 2;
    return word;
}

void CPU6502::pushByte(const uint8_t value)
{
    mem_.write(0x0100 + reg.SP, value);
    reg.SP--;
    cycles_++;
}

uint8_t CPU6502::pullByte()
{
    reg.SP++;
    cycles_++;
    return mem_.read(0x0100 + reg.SP);
}

bool CPU6502::executeSingleInstruction()
{
    const uint8_t opcode = readByte();

    const auto it = handlers_.find(opcode);
    if (it != handlers_.end()) {
        it->second();
        return true;
    } else {
        // Unknown opcode encountered
        return false;
    }
}

void CPU6502::printStatus() const
{
    // Status updates are now handled by the UI framework
    // The MainWindow's updateCpuStatusSidebar() method reads CPU registers directly
}

uint64_t CPU6502::getCycles() const
{
    return cycles_;
}

uint8_t CPU6502::getCurrentByte() const
{
    return mem_.read(reg.PC);
}

// Addressing calculation functions
std::pair<uint16_t, bool> CPU6502::calculateAddress(const bool use_one_byte, const uint8_t offset)
{
    uint16_t address = 0;
    bool page_crossed = false;

    if (use_one_byte)
    {
        // Zero page addressing
        address = (mem_.read(reg.PC) + offset) & 0xFF;
    } else
    {
        // Absolute addressing
        const uint16_t base_address = mem_.readWord(reg.PC);
        address = base_address + offset;

        // Check for page boundary crossing
        if (offset != 0)
        {
            page_crossed = checkPageBoundaryCrossed(base_address, address);
        }

        // Validate address
        validateAddress(address);
    }

    return std::make_pair(address, page_crossed);
}

uint16_t CPU6502::calculateAddressSimple(const bool use_one_byte, const uint8_t offset)
{
    // Wrapper for backward compatibility - returns just the address
    auto [address, page_crossed] = calculateAddress(use_one_byte, offset);
    return address;
}

std::pair<uint16_t, bool> CPU6502::calculateRelativeAddress(const uint8_t offset)
{
    // Calculate the new PC after the branch
    const uint16_t current_pc = reg.PC; // PC already points to instruction after branch

    // Convert signed offset (if > 127, it's negative)
    const int8_t signed_offset = (offset < 0x80) ? offset : offset - 0x100;

    // Calculate target address
    uint16_t target_pc = current_pc + signed_offset;

    // Check if we crossed a page boundary
    bool page_crossed = checkPageBoundaryCrossed(current_pc, target_pc);

    return std::make_pair(target_pc, page_crossed);
}

std::pair<uint16_t, uint8_t> CPU6502::calculateIndexedAddress(const uint8_t offset)
{
    // This is for indexed indirect addressing (zp,X)
    // No additional cycles for page boundary crossing in this mode
    uint8_t addcycle = 0;

    // Get the zero page address from PC and add offset
    const uint8_t zp_addr = mem_.read(reg.PC);
    const uint8_t zp_final = (zp_addr + offset) & 0xFF;

    // Read the 16-bit address from (zp+X) and (zp+X+1)
    uint16_t address = mem_.read(zp_final) | (mem_.read((zp_final + 1) & 0xFF) << 8);

    // Validate address
    validateAddress(address);

    return std::make_pair(address, addcycle);
}

std::pair<uint16_t, uint8_t> CPU6502::calculateIndirectAddress(const uint8_t offset)
{
    // This holds any additional cycle timing due to page boundary crossing
    uint8_t addcycle = 0;

    // Get the zero page address from PC
    const uint8_t zp_addr = mem_.read(reg.PC) & 0xFF;

    // Read the 16-bit base address from (zp) and (zp+1)
    const uint16_t base_address = mem_.read(zp_addr) | (mem_.read((zp_addr + 1) & 0xFF) << 8);

    // Calculate final address by adding offset
    uint16_t address = base_address + offset;

    // Check if we crossed a page boundary
    if (checkPageBoundaryCrossed(base_address, address))
    {
        addcycle = 1;
    }

    // Validate address
    validateAddress(address);

    return std::make_pair(address, addcycle);
}

bool CPU6502::checkPageBoundaryCrossed(const uint16_t base_addr, const uint16_t final_addr)
{
    // Check if addresses are on different pages (different high bytes)
    return (base_addr & 0xFF00) != (final_addr & 0xFF00);
}

bool CPU6502::validateAddress(const uint16_t address)
{
    if (address > 0xFFFF)
    {
        // Invalid address accessed
        return false;
    }
    return true;
}

uint16_t CPU6502::calculateAbsoluteIndirectAddress()
{
    // Read 16-bit absolute address from instruction stream
    const uint16_t indirect_addr = readWord();

    // Read the actual target address from the indirect address
    const uint16_t target_addr = mem_.readWord(indirect_addr);

    validateAddress(target_addr);
    return target_addr;
}

uint16_t CPU6502::calculateAbsoluteIndexedIndirectAddress()
{
    // Read 16-bit absolute address from instruction stream
    const uint16_t base_addr = readWord();

    // Add X register to base address (no page boundary check needed for 65C02)
    const uint16_t indexed_addr = (base_addr + reg.X) & 0xFFFF;

    // Read the actual target address from the indexed address
    const uint16_t target_addr = mem_.readWord(indexed_addr);

    validateAddress(target_addr);
    return target_addr;
}

// ALU helper functions
uint8_t CPU6502::addValues(const uint8_t val1, const uint8_t val2)
{
    const int carry_in = getFlag(kCarry) ? 1 : 0;

    if (getFlag(kDecimal))
    {
        // 65C02 BCD add, nibble-wise with per-nibble adjust
        // (http://www.6502.org/tutorials/decimal_mode.html). N and Z are set
        // from the final result by the caller; here we set V and C.
        int al = (val1 & 0x0F) + (val2 & 0x0F) + carry_in;
        if (al >= 0x0A)
        {
            al = ((al + 0x06) & 0x0F) + 0x10;
        }
        int a = (val1 & 0xF0) + (val2 & 0xF0) + al;

        // Overflow is taken from the intermediate sum, before the high adjust.
        const uint8_t res_pre = static_cast<uint8_t>(a);
        setFlag(kOverflow, ((~(val1 ^ val2) & (val1 ^ res_pre)) & 0x80) != 0);

        if (a >= 0xA0)
        {
            a += 0x60;
        }
        setFlag(kCarry, a >= 0x100);
        return static_cast<uint8_t>(a & 0xFF);
    }

    // Binary mode: A + M + C.
    // V is set when both operands have the same sign but the result's sign
    // differs (signed overflow): ~(val1^val2) & (val1^result) & $80.
    const uint16_t result = val1 + val2 + carry_in;
    const uint8_t res8 = result & 0xFF;
    setFlag(kCarry, (result > 0xFF));
    setFlag(kOverflow, ((~(val1 ^ val2) & (val1 ^ res8)) & 0x80) != 0);
    return res8;
}

uint8_t CPU6502::subtractValues(const uint8_t val1, const uint8_t val2)
{
    const int carry_in = getFlag(kCarry) ? 1 : 0;

    // Carry and overflow derive from the binary subtraction in BOTH decimal and
    // binary modes (this matches real 6502/65C02 SBC behaviour). Carry is set
    // when there is no borrow; V on signed overflow:
    // (val1^val2) & (val1^result) & $80.
    const int bin = val1 - val2 - (1 - carry_in);
    const uint8_t res8 = static_cast<uint8_t>(bin);
    setFlag(kCarry, bin >= 0);
    setFlag(kOverflow, (((val1 ^ val2) & (val1 ^ res8)) & 0x80) != 0);

    if (getFlag(kDecimal))
    {
        // 65C02 BCD subtract, nibble-wise with per-nibble adjust
        // (http://www.6502.org/tutorials/decimal_mode.html).
        int al = (val1 & 0x0F) - (val2 & 0x0F) + carry_in - 1;
        if (al < 0)
        {
            al = ((al - 0x06) & 0x0F) - 0x10;
        }
        int a = (val1 & 0xF0) - (val2 & 0xF0) + al;
        if (a < 0)
        {
            a -= 0x60;
        }
        return static_cast<uint8_t>(a & 0xFF);
    }

    return res8;
}

void CPU6502::compareValues(const uint8_t val1, const uint8_t val2)
{
    // CMP/CPX/CPY compute (val1 - val2) and set flags from the result.
    // N must come from bit 7 of the 8-bit difference, NOT from val1 itself,
    // otherwise CMP-then-BMI/BPL idioms (e.g. EhBASIC's FP exponent alignment)
    // branch the wrong way.
    const uint8_t diff = static_cast<uint8_t>(val1 - val2);
    setFlag(kCarry, (val1 >= val2));
    setFlag(kZero, (val1 == val2));
    setFlag(kNegative, (diff & 0x80) != 0);
}

// Stack helper functions
void CPU6502::pushStack16(const uint16_t value)
{
    // Push the high byte first (6502 pushes MSB first)
    pushByte((value >> 8) & 0xFF);

    // Push the low byte second
    pushByte(value & 0xFF);
}

uint16_t CPU6502::popStack16()
{
    // Pull low byte first (6502 pulls LSB first)
    const uint8_t low_byte = pullByte();

    // Pull high byte second
    const uint8_t high_byte = pullByte();

    return (high_byte << 8) | low_byte;
}

// ADC instruction family handlers
void CPU6502::handleAdcImmediate()
{
    handleAdcBase(reg.PC, 1, 2);
}

void CPU6502::handleAdcZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleAdcBase(address, 1, 3);
}

void CPU6502::handleAdcZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleAdcBase(address, 1, 4);
}

void CPU6502::handleAdcAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleAdcBase(address, 2, 4);
}

void CPU6502::handleAdcAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleAdcBase(address, 2, cycles);
}

void CPU6502::handleAdcAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleAdcBase(address, 2, cycles);
}

void CPU6502::handleAdcIndexedIndirect()
{
    auto [address, cycle] = calculateIndexedAddress(reg.X);
    handleAdcBase(address, 1, 6);
}

void CPU6502::handleAdcIndirectIndexed()
{
    auto [address, cycle] = calculateIndirectAddress(reg.Y);
    const uint8_t cycles = 5 + cycle;
    handleAdcBase(address, 1, cycles);
}

void CPU6502::handleAdcBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    reg.A = addValues(reg.A, val);
    updateZeroNegativeFlags(reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// AND instruction family handlers
void CPU6502::handleAndImmediate()
{
    handleAndBase(reg.PC, 1, 2);
}

void CPU6502::handleAndZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleAndBase(address, 1, 3);
}

void CPU6502::handleAndZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleAndBase(address, 1, 4);
}

void CPU6502::handleAndAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleAndBase(address, 2, 4);
}

void CPU6502::handleAndAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleAndBase(address, 2, cycles);
}

void CPU6502::handleAndAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleAndBase(address, 2, cycles);
}

void CPU6502::handleAndIndexedIndirect()
{
    auto [address, cycle] = calculateIndexedAddress(reg.X);
    handleAndBase(address, 1, 6);
}

void CPU6502::handleAndIndirectIndexed()
{
    auto [address, cycle] = calculateIndirectAddress(reg.Y);
    const uint8_t cycles = 5 + cycle;
    handleAndBase(address, 1, cycles);
}

void CPU6502::handleAndBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    reg.A &= val;
    updateZeroNegativeFlags(reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// ASL instruction family handlers
void CPU6502::handleAslAccumulator()
{
    // Take the high bit of accumulator and set it to Carry flag
    setFlag(kCarry, (reg.A & 0x80) != 0);

    // Rotate the value to the left << one place. Bit 0 is set to 0
    reg.A = (reg.A << 1) & 0xFE;

    // Update flags
    updateZeroNegativeFlags(reg.A);

    // Update cycle counter
    cycles_ += 2;
}

void CPU6502::handleAslZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleAslBase(address, 1, 5);
}

void CPU6502::handleAslZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleAslBase(address, 1, 6);
}

void CPU6502::handleAslAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleAslBase(address, 2, 6);
}

void CPU6502::handleAslAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleAslBase(address, 2, 7);
}

void CPU6502::handleAslBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    uint8_t val = mem_.read(address);

    // Take the high bit of value and set it to Carry flag
    setFlag(kCarry, (val & 0x80) != 0);

    // Rotate the value to the left << one place. Bit 0 is set to 0
    val = (val << 1) & 0xFE;

    // Write the value back to memory
    mem_.write(address, val);

    // Update flags
    updateZeroNegativeFlags(val);

    // Update program and cycle counters
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// Branch instruction handlers
void CPU6502::handleBra()
{
    // BRA rel - $80: Branch always
    const uint8_t offset = readByte();
    auto [target_pc, page_crossed] = calculateRelativeAddress(offset);

    reg.PC = target_pc;
    cycles_ += 2 + (page_crossed ? 1 : 0);
}

void CPU6502::handleBcc()
{
    // Branch if Carry Clear
    const uint8_t offset = readByte();

    if (!getFlag(kCarry))
    {
        auto [target_pc, page_crossed] = calculateRelativeAddress(offset);
        reg.PC = target_pc;
        cycles_ += 3 + (page_crossed ? 1 : 0);
    }
    else
    {
        cycles_ += 2;
    }
}

void CPU6502::handleBcs()
{
    // Branch if Carry Set
    const uint8_t offset = readByte();

    if (getFlag(kCarry))
    {
        auto [target_pc, page_crossed] = calculateRelativeAddress(offset);
        reg.PC = target_pc;
        cycles_ += 3 + (page_crossed ? 1 : 0);
    }
    else
    {
        cycles_ += 2;
    }
}

void CPU6502::handleBeq()
{
    // Branch if Equal (Zero Set)
    const uint8_t offset = readByte();

    if (getFlag(kZero))
    {
        auto [target_pc, page_crossed] = calculateRelativeAddress(offset);
        reg.PC = target_pc;
        cycles_ += 3 + (page_crossed ? 1 : 0);
    }
    else
    {
        cycles_ += 2;
    }
}

void CPU6502::handleBmi()
{
    // Branch if Minus (Negative Set)
    const uint8_t offset = readByte();

    if (getFlag(kNegative))
    {
        auto [target_pc, page_crossed] = calculateRelativeAddress(offset);
        reg.PC = target_pc;
        cycles_ += 3 + (page_crossed ? 1 : 0);
    }
    else
    {
        cycles_ += 2;
    }
}

void CPU6502::handleBne()
{
    // Branch if Not Equal (Zero Clear)
    const uint8_t offset = readByte();

    if (!getFlag(kZero))
    {
        auto [target_pc, page_crossed] = calculateRelativeAddress(offset);
        reg.PC = target_pc;
        cycles_ += 3 + (page_crossed ? 1 : 0);
    }
    else
    {
        cycles_ += 2;
    }
}

void CPU6502::handleBpl()
{
    // Branch if Plus (Negative Clear)
    const uint8_t offset = readByte();

    if (!getFlag(kNegative))
    {
        auto [target_pc, page_crossed] = calculateRelativeAddress(offset);
        reg.PC = target_pc;
        cycles_ += 3 + (page_crossed ? 1 : 0);
    }
    else
    {
        cycles_ += 2;
    }
}

void CPU6502::handleBvc()
{
    // Branch if Overflow Clear
    const uint8_t offset = readByte();

    if (!getFlag(kOverflow))
    {
        auto [target_pc, page_crossed] = calculateRelativeAddress(offset);
        reg.PC = target_pc;
        cycles_ += 3 + (page_crossed ? 1 : 0);
    }
    else
    {
        cycles_ += 2;
    }
}

void CPU6502::handleBvs()
{
    // Branch if Overflow Set
    const uint8_t offset = readByte();

    if (getFlag(kOverflow))
    {
        auto [target_pc, page_crossed] = calculateRelativeAddress(offset);
        reg.PC = target_pc;
        cycles_ += 3 + (page_crossed ? 1 : 0);
    }
    else
    {
        cycles_ += 2;
    }
}

void CPU6502::handleBrk()
{
    // Increment program counter by 2
    reg.PC += 2;

    // Set break flag
    setFlag(kBreak, true);

    // Store the pc on the stack
    pushStack16(reg.PC);

    // Store the processor flags on the stack
    pushByte(reg.P);

    // Set the interrupt flag
    setFlag(kInterrupt, true);

    // Load the pc with the interrupt vector contents at $FFFE/$FFFF
    reg.PC = mem_.readWord(0xFFFE);

    // BRK takes 7 cycles
    cycles_ += 7;
}

// Flag manipulation instruction handlers
void CPU6502::handleClc()
{
    // Clear Carry flag
    setFlag(kCarry, false);
    cycles_ += 2;
}

void CPU6502::handleCld()
{
    // Clear Decimal flag
    setFlag(kDecimal, false);
    cycles_ += 2;
}

void CPU6502::handleCli()
{
    // Clear Interrupt flag
    setFlag(kInterrupt, false);
    cycles_ += 2;
}

void CPU6502::handleClv()
{
    // Clear Overflow flag
    setFlag(kOverflow, false);
    cycles_ += 2;
}

void CPU6502::handleSec()
{
    // Set Carry flag
    setFlag(kCarry, true);
    cycles_ += 2;
}

void CPU6502::handleSed()
{
    // Set Decimal flag
    setFlag(kDecimal, true);
    cycles_ += 2;
}

void CPU6502::handleSei()
{
    // Set Interrupt flag
    setFlag(kInterrupt, true);
    cycles_ += 2;
}

// Stack instruction handlers
void CPU6502::handlePha()
{
    // Push Accumulator onto stack
    pushByte(reg.A);
    cycles_ += 2;
}

void CPU6502::handlePhp()
{
    // Push Processor status onto stack
    pushByte(reg.P | kBreak | kUnused); // Break and unused flags are set when pushed
    cycles_ += 2;
}

void CPU6502::handlePla()
{
    // Pull Accumulator from stack
    reg.A = pullByte();
    updateZeroNegativeFlags(reg.A);
    cycles_ += 3;
}

void CPU6502::handlePlp()
{
    // Pull Processor status from stack
    reg.P = pullByte() & ~(kBreak | kUnused); // Clear break and unused flags when pulled
    reg.P |= kUnused; // Unused flag is always set
    cycles_ += 3;
}

void CPU6502::handlePhx()
{
    // Push X Register onto stack
    pushByte(reg.X);
    cycles_ += 3;
}

void CPU6502::handlePlx()
{
    // Pull X Register from stack
    reg.X = pullByte();
    updateZeroNegativeFlags(reg.X);
    cycles_ += 4;
}

void CPU6502::handlePhy()
{
    // Push Y Register onto stack
    pushByte(reg.Y);
    cycles_ += 3;
}

void CPU6502::handlePly()
{
    // Pull Y Register from stack
    reg.Y = pullByte();
    updateZeroNegativeFlags(reg.Y);
    cycles_ += 4;
}

// Transfer instruction handlers
void CPU6502::handleTax()
{
    // Transfer Accumulator to X
    reg.X = reg.A;
    updateZeroNegativeFlags(reg.X);
    cycles_ += 2;
}

void CPU6502::handleTay()
{
    // Transfer Accumulator to Y
    reg.Y = reg.A;
    updateZeroNegativeFlags(reg.Y);
    cycles_ += 2;
}

void CPU6502::handleTsx()
{
    // Transfer Stack Pointer to X
    reg.X = reg.SP;
    updateZeroNegativeFlags(reg.X);
    cycles_ += 2;
}

void CPU6502::handleTxa()
{
    // Transfer X to Accumulator
    reg.A = reg.X;
    updateZeroNegativeFlags(reg.A);
    cycles_ += 2;
}

void CPU6502::handleTxs()
{
    // Transfer X to Stack Pointer (no flags affected)
    reg.SP = reg.X;
    cycles_ += 2;
}

void CPU6502::handleTya()
{
    // Transfer Y to Accumulator
    reg.A = reg.Y;
    updateZeroNegativeFlags(reg.A);
    cycles_ += 2;
}

void CPU6502::handleNop()
{
    cycles_ += 2;
}

// LDA instruction family handlers
void CPU6502::handleLdaImmediate()
{
    handleLdaBase(reg.PC, 1, 2);
}

void CPU6502::handleLdaZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleLdaBase(address, 1, 3);
}

void CPU6502::handleLdaZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleLdaBase(address, 1, 4);
}

void CPU6502::handleLdaAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleLdaBase(address, 2, 4);
}

void CPU6502::handleLdaAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleLdaBase(address, 2, cycles);
}

void CPU6502::handleLdaAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleLdaBase(address, 2, cycles);
}

void CPU6502::handleLdaIndexedIndirect()
{
    auto [address, cycle] = calculateIndexedAddress(reg.X);
    handleLdaBase(address, 1, 6);
}

void CPU6502::handleLdaIndirectIndexed()
{
    auto [address, cycle] = calculateIndirectAddress(reg.Y);
    const uint8_t cycles = 5 + cycle;
    handleLdaBase(address, 1, cycles);
}

void CPU6502::handleLdaBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    reg.A = val;
    updateZeroNegativeFlags(reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// JMP instruction handlers
void CPU6502::handleJmpAbsolute()
{
    handleJmpBase(readWord(), 3);
}

void CPU6502::handleJmpIndirect()
{
    const uint16_t indirect_addr = readWord();
    const uint16_t target_addr = mem_.readWord(indirect_addr);
    handleJmpBase(target_addr, 5);
}

void CPU6502::handleJmpBase(const uint16_t address, const uint8_t cycles)
{
    reg.PC = address;
    cycles_ += cycles;
}

// STA instruction family handlers
void CPU6502::handleStaZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleStaBase(address, 1, 3);
}

void CPU6502::handleStaZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleStaBase(address, 1, 4);
}

void CPU6502::handleStaAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleStaBase(address, 2, 4);
}

void CPU6502::handleStaAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleStaBase(address, 2, 5); // STA always takes extra cycle for indexed addressing
}

void CPU6502::handleStaAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    handleStaBase(address, 2, 5); // STA always takes extra cycle for indexed addressing
}

void CPU6502::handleStaIndexedIndirect()
{
    auto [address, cycle] = calculateIndexedAddress(reg.X);
    handleStaBase(address, 1, 6);
}

void CPU6502::handleStaIndirectIndexed()
{
    auto [address, cycle] = calculateIndirectAddress(reg.Y);
    handleStaBase(address, 1, 6); // STA always takes 6 cycles for this mode
}

void CPU6502::handleStaBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    mem_.write(address, reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// JSR instruction handler
void CPU6502::handleJsr()
{
    // Calculate return address before reading target (PC currently points to low byte of target)
    const uint16_t return_address = reg.PC + 1; // Address of last byte of JSR instruction

    // Read target address (this advances PC by 2)
    const uint16_t target_address = readWord();

    // Push return address onto stack (6502 pushes PC+2 from start of JSR)
    pushStack16(return_address);

    // Jump to target address
    reg.PC = target_address;
    cycles_ += 6;
}

// RTS instruction handler
void CPU6502::handleRts()
{
    // Pull return address from stack
    const uint16_t return_address = popStack16();

    // Set PC to return address + 1
    reg.PC = return_address + 1;
    cycles_ += 6;
}

// RTI instruction handler
void CPU6502::handleRti()
{
    // Pull processor status from stack
    reg.P = pullByte() & ~(kBreak | kUnused); // Clear break and unused flags when pulled
    reg.P |= kUnused; // Unused flag is always set

    // Pull return address from stack
    reg.PC = popStack16();

    cycles_ += 6;
}

// LDX instruction family handlers
void CPU6502::handleLdxImmediate()
{
    handleLdxBase(reg.PC, 1, 2);
}

void CPU6502::handleLdxZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleLdxBase(address, 1, 3);
}

void CPU6502::handleLdxZeroPageY()
{
    const uint16_t address = calculateAddressSimple(true, reg.Y);
    handleLdxBase(address, 1, 4);
}

void CPU6502::handleLdxAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleLdxBase(address, 2, 4);
}

void CPU6502::handleLdxAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleLdxBase(address, 2, cycles);
}

void CPU6502::handleLdxBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    reg.X = val;
    updateZeroNegativeFlags(reg.X);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// LDY instruction family handlers
void CPU6502::handleLdyImmediate()
{
    handleLdyBase(reg.PC, 1, 2);
}

void CPU6502::handleLdyZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleLdyBase(address, 1, 3);
}

void CPU6502::handleLdyZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleLdyBase(address, 1, 4);
}

void CPU6502::handleLdyAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleLdyBase(address, 2, 4);
}

void CPU6502::handleLdyAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleLdyBase(address, 2, cycles);
}

void CPU6502::handleLdyBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    reg.Y = val;
    updateZeroNegativeFlags(reg.Y);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// STX instruction family handlers
void CPU6502::handleStxZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleStxBase(address, 1, 3);
}

void CPU6502::handleStxZeroPageY()
{
    const uint16_t address = calculateAddressSimple(true, reg.Y);
    handleStxBase(address, 1, 4);
}

void CPU6502::handleStxAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleStxBase(address, 2, 4);
}

void CPU6502::handleStxBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    mem_.write(address, reg.X);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// STY instruction family handlers
void CPU6502::handleStyZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleStyBase(address, 1, 3);
}

void CPU6502::handleStyZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleStyBase(address, 1, 4);
}

void CPU6502::handleStyAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleStyBase(address, 2, 4);
}

void CPU6502::handleStyBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    mem_.write(address, reg.Y);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// CMP instruction family handlers
void CPU6502::handleCmpImmediate()
{
    handleCmpBase(reg.PC, 1, 2);
}

void CPU6502::handleCmpZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleCmpBase(address, 1, 3);
}

void CPU6502::handleCmpZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleCmpBase(address, 1, 4);
}

void CPU6502::handleCmpAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleCmpBase(address, 2, 4);
}

void CPU6502::handleCmpAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleCmpBase(address, 2, cycles);
}

void CPU6502::handleCmpAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleCmpBase(address, 2, cycles);
}

void CPU6502::handleCmpIndexedIndirect()
{
    auto [address, cycle] = calculateIndexedAddress(reg.X);
    handleCmpBase(address, 1, 6);
}

void CPU6502::handleCmpIndirectIndexed()
{
    auto [address, cycle] = calculateIndirectAddress(reg.Y);
    const uint8_t cycles = 5 + cycle;
    handleCmpBase(address, 1, cycles);
}

void CPU6502::handleCmpBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    compareValues(reg.A, val);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// CPX instruction family handlers
void CPU6502::handleCpxImmediate()
{
    handleCpxBase(reg.PC, 1, 2);
}

void CPU6502::handleCpxZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleCpxBase(address, 1, 3);
}

void CPU6502::handleCpxAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleCpxBase(address, 2, 4);
}

void CPU6502::handleCpxBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    compareValues(reg.X, val);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// CPY instruction family handlers
void CPU6502::handleCpyImmediate()
{
    handleCpyBase(reg.PC, 1, 2);
}

void CPU6502::handleCpyZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleCpyBase(address, 1, 3);
}

void CPU6502::handleCpyAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleCpyBase(address, 2, 4);
}

void CPU6502::handleCpyBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    compareValues(reg.Y, val);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// SBC instruction family handlers
void CPU6502::handleSbcImmediate()
{
    handleSbcBase(reg.PC, 1, 2);
}

void CPU6502::handleSbcZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleSbcBase(address, 1, 3);
}

void CPU6502::handleSbcZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleSbcBase(address, 1, 4);
}

void CPU6502::handleSbcAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleSbcBase(address, 2, 4);
}

void CPU6502::handleSbcAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleSbcBase(address, 2, cycles);
}

void CPU6502::handleSbcAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleSbcBase(address, 2, cycles);
}

void CPU6502::handleSbcIndexedIndirect()
{
    auto [address, cycle] = calculateIndexedAddress(reg.X);
    handleSbcBase(address, 1, 6);
}

void CPU6502::handleSbcIndirectIndexed()
{
    auto [address, cycle] = calculateIndirectAddress(reg.Y);
    const uint8_t cycles = 5 + cycle;
    handleSbcBase(address, 1, cycles);
}

void CPU6502::handleSbcBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    reg.A = subtractValues(reg.A, val);
    updateZeroNegativeFlags(reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// EOR instruction family handlers
void CPU6502::handleEorImmediate()
{
    handleEorBase(reg.PC, 1, 2);
}

void CPU6502::handleEorZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleEorBase(address, 1, 3);
}

void CPU6502::handleEorZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleEorBase(address, 1, 4);
}

void CPU6502::handleEorAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleEorBase(address, 2, 4);
}

void CPU6502::handleEorAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleEorBase(address, 2, cycles);
}

void CPU6502::handleEorAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleEorBase(address, 2, cycles);
}

void CPU6502::handleEorIndexedIndirect()
{
    auto [address, cycle] = calculateIndexedAddress(reg.X);
    handleEorBase(address, 1, 6);
}

void CPU6502::handleEorIndirectIndexed()
{
    auto [address, cycle] = calculateIndirectAddress(reg.Y);
    const uint8_t cycles = 5 + cycle;
    handleEorBase(address, 1, cycles);
}

void CPU6502::handleEorBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    reg.A ^= val;  // Exclusive OR operation
    updateZeroNegativeFlags(reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// ORA instruction family handlers
void CPU6502::handleOraImmediate()
{
    handleOraBase(reg.PC, 1, 2);
}

void CPU6502::handleOraZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleOraBase(address, 1, 3);
}

void CPU6502::handleOraZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleOraBase(address, 1, 4);
}

void CPU6502::handleOraAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleOraBase(address, 2, 4);
}

void CPU6502::handleOraAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleOraBase(address, 2, cycles);
}

void CPU6502::handleOraAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleOraBase(address, 2, cycles);
}

void CPU6502::handleOraIndexedIndirect()
{
    auto [address, cycle] = calculateIndexedAddress(reg.X);
    handleOraBase(address, 1, 6);
}

void CPU6502::handleOraIndirectIndexed()
{
    auto [address, cycle] = calculateIndirectAddress(reg.Y);
    const uint8_t cycles = 5 + cycle;
    handleOraBase(address, 1, cycles);
}

void CPU6502::handleOraBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    reg.A |= val;  // Logical OR operation
    updateZeroNegativeFlags(reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// BIT instruction family handlers
void CPU6502::handleBitZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleBitBase(address, 1, 3);
}

void CPU6502::handleBitAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleBitBase(address, 2, 4);
}

    void CPU6502::handleBitImmediate()
{
    // BIT # - $89: Bit test immediate (only affects zero flag)
    const uint8_t val = readByte();
    const uint8_t result = reg.A & val;

    // Only zero flag is affected in immediate mode
    setFlag(kZero, result == 0);

    cycles_ += 2;
}

    void CPU6502::handleBitZeroPageX()
{
    // BIT zp,X - $34: Bit test zero page,X
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleBitBase(address, 1, 4);
}

    void CPU6502::handleBitAbsoluteX()
{
    // BIT abs,X - $3C: Bit test absolute,X
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    const uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleBitBase(address, 2, cycles);
}

void CPU6502::handleBitBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    const uint8_t val = mem_.read(address);
    const uint8_t result = reg.A & val;

    // Set zero flag based on AND result
    setFlag(kZero, result == 0);

    // Transfer bit 7 of memory to negative flag
    setFlag(kNegative, (val & 0x80) != 0);

    // Transfer bit 6 of memory to overflow flag
    setFlag(kOverflow, (val & 0x40) != 0);

    reg.PC += pc_offset;
    cycles_ += cycles;
}

// LSR instruction family handlers
void CPU6502::handleLsrAccumulator()
{
    // Set carry flag to bit 0 of accumulator
    setFlag(kCarry, (reg.A & 0x01) != 0);

    // Shift right by one bit
    reg.A = reg.A >> 1;

    // Update flags
    updateZeroNegativeFlags(reg.A);

    cycles_ += 2;
}

void CPU6502::handleLsrZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleLsrBase(address, 1, 5);
}

void CPU6502::handleLsrZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleLsrBase(address, 1, 6);
}

void CPU6502::handleLsrAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleLsrBase(address, 2, 6);
}

void CPU6502::handleLsrAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleLsrBase(address, 2, 7);
}

void CPU6502::handleLsrBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    uint8_t val = mem_.read(address);

    // Set carry flag to bit 0 of value
    setFlag(kCarry, (val & 0x01) != 0);

    // Shift right by one bit
    val = val >> 1;

    // Write back to memory
    mem_.write(address, val);

    // Update flags
    updateZeroNegativeFlags(val);

    reg.PC += pc_offset;
    cycles_ += cycles;
}

// ROL instruction family handlers
void CPU6502::handleRolAccumulator()
{
    const bool old_carry = getFlag(kCarry);

    // Set carry flag to bit 7 of accumulator
    setFlag(kCarry, (reg.A & 0x80) != 0);

    // Rotate left, with old carry becoming bit 0
    reg.A = (reg.A << 1) | (old_carry ? 1 : 0);

    // Update flags
    updateZeroNegativeFlags(reg.A);

    cycles_ += 2;
}

void CPU6502::handleRolZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleRolBase(address, 1, 5);
}

void CPU6502::handleRolZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleRolBase(address, 1, 6);
}

void CPU6502::handleRolAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleRolBase(address, 2, 6);
}

void CPU6502::handleRolAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleRolBase(address, 2, 7);
}

void CPU6502::handleRolBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    const bool old_carry = getFlag(kCarry);

    // Set carry flag to bit 7 of value
    setFlag(kCarry, (val & 0x80) != 0);

    // Rotate left, with old carry becoming bit 0
    val = (val << 1) | (old_carry ? 1 : 0);

    // Write back to memory
    mem_.write(address, val);

    // Update flags
    updateZeroNegativeFlags(val);

    reg.PC += pc_offset;
    cycles_ += cycles;
}

// ROR instruction family handlers
void CPU6502::handleRorAccumulator()
{
    const bool old_carry = getFlag(kCarry);

    // Set carry flag to bit 0 of accumulator
    setFlag(kCarry, (reg.A & 0x01) != 0);

    // Rotate right, with old carry becoming bit 7
    reg.A = (reg.A >> 1) | (old_carry ? 0x80 : 0);

    // Update flags
    updateZeroNegativeFlags(reg.A);

    cycles_ += 2;
}

void CPU6502::handleRorZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleRorBase(address, 1, 5);
}

void CPU6502::handleRorZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleRorBase(address, 1, 6);
}

void CPU6502::handleRorAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleRorBase(address, 2, 6);
}

void CPU6502::handleRorAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleRorBase(address, 2, 7);
}

void CPU6502::handleRorBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    const bool old_carry = getFlag(kCarry);

    // Set carry flag to bit 0 of value
    setFlag(kCarry, (val & 0x01) != 0);

    // Rotate right, with old carry becoming bit 7
    val = (val >> 1) | (old_carry ? 0x80 : 0);

    // Write back to memory
    mem_.write(address, val);

    // Update flags
    updateZeroNegativeFlags(val);

    reg.PC += pc_offset;
    cycles_ += cycles;
}

// INC instruction family handlers
void CPU6502::handleIncZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleIncBase(address, 1, 5);
}

void CPU6502::handleIncZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleIncBase(address, 1, 6);
}

void CPU6502::handleIncAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleIncBase(address, 2, 6);
}

void CPU6502::handleIncAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleIncBase(address, 2, 7);
}

void CPU6502::handleIncBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    val++;
    mem_.write(address, val);
    updateZeroNegativeFlags(val);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// DEC instruction family handlers
void CPU6502::handleDecZeroPage()
{
    const uint16_t address = calculateAddressSimple(true, 0);
    handleDecBase(address, 1, 5);
}

void CPU6502::handleDecZeroPageX()
{
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleDecBase(address, 1, 6);
}

void CPU6502::handleDecAbsolute()
{
    const uint16_t address = calculateAddressSimple(false, 0);
    handleDecBase(address, 2, 6);
}

void CPU6502::handleDecAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleDecBase(address, 2, 7);
}

void CPU6502::handleDecBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    val--;
    mem_.write(address, val);
    updateZeroNegativeFlags(val);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// Register increment/decrement handlers
void CPU6502::handleInx()
{
    reg.X++;
    updateZeroNegativeFlags(reg.X);
    cycles_ += 2;
}

void CPU6502::handleIny()
{
    reg.Y++;
    updateZeroNegativeFlags(reg.Y);
    cycles_ += 2;
}

void CPU6502::handleDex()
{
    reg.X--;
    updateZeroNegativeFlags(reg.X);
    cycles_ += 2;
}

void CPU6502::handleDey()
{
    reg.Y--;
    updateZeroNegativeFlags(reg.Y);
    cycles_ += 2;
}

void CPU6502::handleIncAccumulator()
{
    // INC A - $1A: Increment accumulator
    reg.A++;
    updateZeroNegativeFlags(reg.A);
    cycles_ += 2;
}

void CPU6502::handleDecAccumulator()
{
    // DEC A - $3A: Decrement accumulator
    reg.A--;
    updateZeroNegativeFlags(reg.A);
    cycles_ += 2;
}

// 65C02 Absolute Indirect addressing modes
void CPU6502::handleAdcAbsoluteIndirect()
{
    // ADC (addr) - $72: Add with carry absolute indirect
    const uint16_t address = calculateAbsoluteIndirectAddress();
    const uint8_t val = mem_.read(address);
    reg.A = addValues(reg.A, val);
    updateZeroNegativeFlags(reg.A);
    cycles_ += 6;  // 65C02 timing
}

void CPU6502::handleAndAbsoluteIndirect()
{
    // AND (addr) - $32: AND absolute indirect
    const uint16_t address = calculateAbsoluteIndirectAddress();
    const uint8_t val = mem_.read(address);
    reg.A &= val;
    updateZeroNegativeFlags(reg.A);
    cycles_ += 6;  // 65C02 timing
}

void CPU6502::handleCmpAbsoluteIndirect()
{
    // CMP (addr) - $D2: Compare absolute indirect
    const uint16_t address = calculateAbsoluteIndirectAddress();
    const uint8_t val = mem_.read(address);
    compareValues(reg.A, val);
    cycles_ += 6;  // 65C02 timing
}

void CPU6502::handleEorAbsoluteIndirect()
{
    // EOR (addr) - $52: Exclusive OR absolute indirect
    const uint16_t address = calculateAbsoluteIndirectAddress();
    const uint8_t val = mem_.read(address);
    reg.A ^= val;
    updateZeroNegativeFlags(reg.A);
    cycles_ += 6;  // 65C02 timing
}

void CPU6502::handleLdaAbsoluteIndirect()
{
    // LDA (addr) - $B2: Load accumulator absolute indirect
    const uint16_t address = calculateAbsoluteIndirectAddress();
    const uint8_t val = mem_.read(address);
    reg.A = val;
    updateZeroNegativeFlags(reg.A);
    cycles_ += 6;  // 65C02 timing
}

void CPU6502::handleOraAbsoluteIndirect()
{
    // ORA (addr) - $12: OR with accumulator absolute indirect
    const uint16_t address = calculateAbsoluteIndirectAddress();
    const uint8_t val = mem_.read(address);
    reg.A |= val;
    updateZeroNegativeFlags(reg.A);
    cycles_ += 6;  // 65C02 timing
}

void CPU6502::handleSbcAbsoluteIndirect()
{
    // SBC (addr) - $F2: Subtract with carry absolute indirect
    const uint16_t address = calculateAbsoluteIndirectAddress();
    const uint8_t val = mem_.read(address);
    reg.A = subtractValues(reg.A, val);
    updateZeroNegativeFlags(reg.A);
    cycles_ += 6;  // 65C02 timing
}

void CPU6502::handleStaAbsoluteIndirect()
{
    // STA (addr) - $92: Store accumulator absolute indirect
    const uint16_t address = calculateAbsoluteIndirectAddress();
    mem_.write(address, reg.A);
    cycles_ += 6;  // 65C02 timing
}

// 65C02 Jump Absolute Indexed Indirect
void CPU6502::handleJmpAbsoluteIndexedIndirect()
{
    // JMP (addr,X) - $7C: Jump absolute indexed indirect
    const uint16_t target_addr = calculateAbsoluteIndexedIndirectAddress();
    reg.PC = target_addr;
    cycles_ += 6;  // 65C02 timing
}

// 65C02 Store Zero instructions
void CPU6502::handleStzZeroPage()
{
    // STZ zp - $64: Store zero to zero page
    const uint16_t address = calculateAddressSimple(true, 0);
    handleStzBase(address, 1, 3);
}

void CPU6502::handleStzZeroPageX()
{
    // STZ zp,X - $74: Store zero to zero page,X
    const uint16_t address = calculateAddressSimple(true, reg.X);
    handleStzBase(address, 1, 4);
}

void CPU6502::handleStzAbsolute()
{
    // STZ abs - $9C: Store zero to absolute
    const uint16_t address = calculateAddressSimple(false, 0);
    handleStzBase(address, 2, 4);
}

void CPU6502::handleStzAbsoluteX()
{
    // STZ abs,X - $9E: Store zero to absolute,X
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleStzBase(address, 2, 5);  // Always 5 cycles for STZ
}

void CPU6502::handleStzBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles)
{
    mem_.write(address, 0x00);  // Store zero
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// 65C02 Test and Reset/Set Bits
void CPU6502::handleTrbZeroPage()
{
    // TRB zp - $14: Test and reset bits zero page
    const uint16_t address = calculateAddressSimple(true, 0);
    handleTrbTsbBase(address, 1, 5, false);
}

void CPU6502::handleTrbAbsolute()
{
    // TRB abs - $1C: Test and reset bits absolute
    const uint16_t address = calculateAddressSimple(false, 0);
    handleTrbTsbBase(address, 2, 6, false);
}

void CPU6502::handleTsbZeroPage()
{
    // TSB zp - $04: Test and set bits zero page
    const uint16_t address = calculateAddressSimple(true, 0);
    handleTrbTsbBase(address, 1, 5, true);
}

void CPU6502::handleTsbAbsolute()
{
    // TSB abs - $0C: Test and set bits absolute
    const uint16_t address = calculateAddressSimple(false, 0);
    handleTrbTsbBase(address, 2, 6, true);
}

void CPU6502::handleTrbTsbBase(const uint16_t address, const uint8_t pc_offset, const uint8_t cycles, const bool is_set)
{
    uint8_t val = mem_.read(address);

    // Test: Set zero flag if (A AND memory) == 0
    setFlag(kZero, (reg.A & val) == 0);

    if (is_set) {
        // TSB: Set bits in memory where accumulator has 1 bits
        val |= reg.A;
    } else {
        // TRB: Reset bits in memory where accumulator has 1 bits
        val &= ~reg.A;
    }

    mem_.write(address, val);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// 65C02 Processor Control
void CPU6502::handleStp()
{
    // STP - $DB: Stop processor
    // In a real implementation, this would halt the CPU
    // For our emulator, we'll just add cycles and continue
    cycles_ += 3;

    // TODO: In a full implementation, this should set a "stopped" flag
    // that prevents further instruction execution until reset
}

void CPU6502::handleWai()
{
    // WAI - $CB: Wait for interrupt
    // In a real implementation, this would wait for IRQ/NMI
    // For our emulator, we'll just add cycles and continue
    cycles_ += 3;

    // TODO: In a full implementation, this should wait for an interrupt
    // and only resume execution when IRQ or NMI occurs
}

void CPU6502::initializeInstructionHandlers()
{
    // System instructions
    handlers_[0x00] = [this]() { handleBrk(); };
    handlers_[0xEA] = [this]() { handleNop(); };
    handlers_[0x20] = [this]() { handleJsr(); };
    handlers_[0x60] = [this]() { handleRts(); };
    handlers_[0x40] = [this]() { handleRti(); };
    
    // LDA instructions
    handlers_[0xA9] = [this]() { handleLdaImmediate(); };
    handlers_[0xA5] = [this]() { handleLdaZeroPage(); };
    handlers_[0xB5] = [this]() { handleLdaZeroPageX(); };
    handlers_[0xAD] = [this]() { handleLdaAbsolute(); };
    handlers_[0xBD] = [this]() { handleLdaAbsoluteX(); };
    handlers_[0xB9] = [this]() { handleLdaAbsoluteY(); };
    handlers_[0xA1] = [this]() { handleLdaIndexedIndirect(); };
    handlers_[0xB1] = [this]() { handleLdaIndirectIndexed(); };
    
    // STA instructions
    handlers_[0x85] = [this]() { handleStaZeroPage(); };
    handlers_[0x95] = [this]() { handleStaZeroPageX(); };
    handlers_[0x8D] = [this]() { handleStaAbsolute(); };
    handlers_[0x9D] = [this]() { handleStaAbsoluteX(); };
    handlers_[0x99] = [this]() { handleStaAbsoluteY(); };
    handlers_[0x81] = [this]() { handleStaIndexedIndirect(); };
    handlers_[0x91] = [this]() { handleStaIndirectIndexed(); };
    
    // JMP instructions
    handlers_[0x4C] = [this]() { handleJmpAbsolute(); };
    handlers_[0x6C] = [this]() { handleJmpIndirect(); };
    
    // AND instructions
    handlers_[0x29] = [this]() { handleAndImmediate(); };
    handlers_[0x25] = [this]() { handleAndZeroPage(); };
    handlers_[0x35] = [this]() { handleAndZeroPageX(); };
    handlers_[0x2D] = [this]() { handleAndAbsolute(); };
    handlers_[0x3D] = [this]() { handleAndAbsoluteX(); };
    handlers_[0x39] = [this]() { handleAndAbsoluteY(); };
    handlers_[0x21] = [this]() { handleAndIndexedIndirect(); };
    handlers_[0x31] = [this]() { handleAndIndirectIndexed(); };
    
    // LDX instructions
    handlers_[0xA2] = [this]() { handleLdxImmediate(); };
    handlers_[0xA6] = [this]() { handleLdxZeroPage(); };
    handlers_[0xB6] = [this]() { handleLdxZeroPageY(); };
    handlers_[0xAE] = [this]() { handleLdxAbsolute(); };
    handlers_[0xBE] = [this]() { handleLdxAbsoluteY(); };
    
    // LDY instructions
    handlers_[0xA0] = [this]() { handleLdyImmediate(); };
    handlers_[0xA4] = [this]() { handleLdyZeroPage(); };
    handlers_[0xB4] = [this]() { handleLdyZeroPageX(); };
    handlers_[0xAC] = [this]() { handleLdyAbsolute(); };
    handlers_[0xBC] = [this]() { handleLdyAbsoluteX(); };
    
    // STX instructions
    handlers_[0x86] = [this]() { handleStxZeroPage(); };
    handlers_[0x96] = [this]() { handleStxZeroPageY(); };
    handlers_[0x8E] = [this]() { handleStxAbsolute(); };
    
    // STY instructions
    handlers_[0x84] = [this]() { handleStyZeroPage(); };
    handlers_[0x94] = [this]() { handleStyZeroPageX(); };
    handlers_[0x8C] = [this]() { handleStyAbsolute(); };
    
    // Branch instructions
    handlers_[0x90] = [this]() { handleBcc(); };
    handlers_[0xB0] = [this]() { handleBcs(); };
    handlers_[0xF0] = [this]() { handleBeq(); };
    handlers_[0x30] = [this]() { handleBmi(); };
    handlers_[0xD0] = [this]() { handleBne(); };
    handlers_[0x10] = [this]() { handleBpl(); };
    handlers_[0x50] = [this]() { handleBvc(); };
    handlers_[0x70] = [this]() { handleBvs(); };
    
    // ADC instructions
    handlers_[0x69] = [this]() { handleAdcImmediate(); };
    handlers_[0x65] = [this]() { handleAdcZeroPage(); };
    handlers_[0x75] = [this]() { handleAdcZeroPageX(); };
    handlers_[0x6D] = [this]() { handleAdcAbsolute(); };
    handlers_[0x7D] = [this]() { handleAdcAbsoluteX(); };
    handlers_[0x79] = [this]() { handleAdcAbsoluteY(); };
    handlers_[0x61] = [this]() { handleAdcIndexedIndirect(); };
    handlers_[0x71] = [this]() { handleAdcIndirectIndexed(); };
    
    // SBC instructions
    handlers_[0xE9] = [this]() { handleSbcImmediate(); };
    handlers_[0xE5] = [this]() { handleSbcZeroPage(); };
    handlers_[0xF5] = [this]() { handleSbcZeroPageX(); };
    handlers_[0xED] = [this]() { handleSbcAbsolute(); };
    handlers_[0xFD] = [this]() { handleSbcAbsoluteX(); };
    handlers_[0xF9] = [this]() { handleSbcAbsoluteY(); };
    handlers_[0xE1] = [this]() { handleSbcIndexedIndirect(); };
    handlers_[0xF1] = [this]() { handleSbcIndirectIndexed(); };
    
    // CMP instructions
    handlers_[0xC9] = [this]() { handleCmpImmediate(); };
    handlers_[0xC5] = [this]() { handleCmpZeroPage(); };
    handlers_[0xD5] = [this]() { handleCmpZeroPageX(); };
    handlers_[0xCD] = [this]() { handleCmpAbsolute(); };
    handlers_[0xDD] = [this]() { handleCmpAbsoluteX(); };
    handlers_[0xD9] = [this]() { handleCmpAbsoluteY(); };
    handlers_[0xC1] = [this]() { handleCmpIndexedIndirect(); };
    handlers_[0xD1] = [this]() { handleCmpIndirectIndexed(); };
    
    // CPX instructions
    handlers_[0xE0] = [this]() { handleCpxImmediate(); };
    handlers_[0xE4] = [this]() { handleCpxZeroPage(); };
    handlers_[0xEC] = [this]() { handleCpxAbsolute(); };
    
    // CPY instructions
    handlers_[0xC0] = [this]() { handleCpyImmediate(); };
    handlers_[0xC4] = [this]() { handleCpyZeroPage(); };
    handlers_[0xCC] = [this]() { handleCpyAbsolute(); };
    
    // EOR instructions
    handlers_[0x49] = [this]() { handleEorImmediate(); };
    handlers_[0x45] = [this]() { handleEorZeroPage(); };
    handlers_[0x55] = [this]() { handleEorZeroPageX(); };
    handlers_[0x4D] = [this]() { handleEorAbsolute(); };
    handlers_[0x5D] = [this]() { handleEorAbsoluteX(); };
    handlers_[0x59] = [this]() { handleEorAbsoluteY(); };
    handlers_[0x41] = [this]() { handleEorIndexedIndirect(); };
    handlers_[0x51] = [this]() { handleEorIndirectIndexed(); };
    
    // ORA instructions
    handlers_[0x09] = [this]() { handleOraImmediate(); };
    handlers_[0x05] = [this]() { handleOraZeroPage(); };
    handlers_[0x15] = [this]() { handleOraZeroPageX(); };
    handlers_[0x0D] = [this]() { handleOraAbsolute(); };
    handlers_[0x1D] = [this]() { handleOraAbsoluteX(); };
    handlers_[0x19] = [this]() { handleOraAbsoluteY(); };
    handlers_[0x01] = [this]() { handleOraIndexedIndirect(); };
    handlers_[0x11] = [this]() { handleOraIndirectIndexed(); };
    
    // BIT instructions
    handlers_[0x24] = [this]() { handleBitZeroPage(); };
    handlers_[0x2C] = [this]() { handleBitAbsolute(); };
    
    // ASL instructions
    handlers_[0x0A] = [this]() { handleAslAccumulator(); };
    handlers_[0x06] = [this]() { handleAslZeroPage(); };
    handlers_[0x16] = [this]() { handleAslZeroPageX(); };
    handlers_[0x0E] = [this]() { handleAslAbsolute(); };
    handlers_[0x1E] = [this]() { handleAslAbsoluteX(); };
    
    // LSR instructions
    handlers_[0x4A] = [this]() { handleLsrAccumulator(); };
    handlers_[0x46] = [this]() { handleLsrZeroPage(); };
    handlers_[0x56] = [this]() { handleLsrZeroPageX(); };
    handlers_[0x4E] = [this]() { handleLsrAbsolute(); };
    handlers_[0x5E] = [this]() { handleLsrAbsoluteX(); };
    
    // ROL instructions
    handlers_[0x2A] = [this]() { handleRolAccumulator(); };
    handlers_[0x26] = [this]() { handleRolZeroPage(); };
    handlers_[0x36] = [this]() { handleRolZeroPageX(); };
    handlers_[0x2E] = [this]() { handleRolAbsolute(); };
    handlers_[0x3E] = [this]() { handleRolAbsoluteX(); };
    
    // ROR instructions
    handlers_[0x6A] = [this]() { handleRorAccumulator(); };
    handlers_[0x66] = [this]() { handleRorZeroPage(); };
    handlers_[0x76] = [this]() { handleRorZeroPageX(); };
    handlers_[0x6E] = [this]() { handleRorAbsolute(); };
    handlers_[0x7E] = [this]() { handleRorAbsoluteX(); };
    
    // INC instructions
    handlers_[0xE6] = [this]() { handleIncZeroPage(); };
    handlers_[0xF6] = [this]() { handleIncZeroPageX(); };
    handlers_[0xEE] = [this]() { handleIncAbsolute(); };
    handlers_[0xFE] = [this]() { handleIncAbsoluteX(); };
    
    // DEC instructions
    handlers_[0xC6] = [this]() { handleDecZeroPage(); };
    handlers_[0xD6] = [this]() { handleDecZeroPageX(); };
    handlers_[0xCE] = [this]() { handleDecAbsolute(); };
    handlers_[0xDE] = [this]() { handleDecAbsoluteX(); };
    
    // Register inc/dec instructions
    handlers_[0xE8] = [this]() { handleInx(); };
    handlers_[0xC8] = [this]() { handleIny(); };
    handlers_[0xCA] = [this]() { handleDex(); };
    handlers_[0x88] = [this]() { handleDey(); };
    
    // Flag manipulation instructions
    handlers_[0x18] = [this]() { handleClc(); };
    handlers_[0xD8] = [this]() { handleCld(); };
    handlers_[0x58] = [this]() { handleCli(); };
    handlers_[0xB8] = [this]() { handleClv(); };
    handlers_[0x38] = [this]() { handleSec(); };
    handlers_[0xF8] = [this]() { handleSed(); };
    handlers_[0x78] = [this]() { handleSei(); };
    
    // Stack instructions
    handlers_[0x48] = [this]() { handlePha(); };
    handlers_[0x08] = [this]() { handlePhp(); };
    handlers_[0x68] = [this]() { handlePla(); };
    handlers_[0x28] = [this]() { handlePlp(); };
    handlers_[0xDA] = [this]() { handlePhx(); };
    handlers_[0xFA] = [this]() { handlePlx(); };
    handlers_[0x5A] = [this]() { handlePhy(); };
    handlers_[0x7A] = [this]() { handlePly(); };
    
    // Transfer instructions
    handlers_[0xAA] = [this]() { handleTax(); };
    handlers_[0xA8] = [this]() { handleTay(); };
    handlers_[0xBA] = [this]() { handleTsx(); };
    handlers_[0x8A] = [this]() { handleTxa(); };
    handlers_[0x9A] = [this]() { handleTxs(); };
    handlers_[0x98] = [this]() { handleTya(); };

    // ================================================================
    // 65C02 NEW INSTRUCTIONS
    // ================================================================

    // 65C02 Accumulator increment/decrement
    handlers_[0x1A] = [this]() { handleIncAccumulator(); };  // INC A
    handlers_[0x3A] = [this]() { handleDecAccumulator(); };  // DEC A

    // 65C02 Enhanced BIT instructions
    handlers_[0x89] = [this]() { handleBitImmediate(); };    // BIT #
    handlers_[0x34] = [this]() { handleBitZeroPageX(); };    // BIT zp,X
    handlers_[0x3C] = [this]() { handleBitAbsoluteX(); };    // BIT abs,X

    // 65C02 Branch Always
    handlers_[0x80] = [this]() { handleBra(); };             // BRA rel

    // 65C02 Absolute Indirect addressing modes
    handlers_[0x72] = [this]() { handleAdcAbsoluteIndirect(); };  // ADC (addr)
    handlers_[0x32] = [this]() { handleAndAbsoluteIndirect(); };  // AND (addr)
    handlers_[0xD2] = [this]() { handleCmpAbsoluteIndirect(); };  // CMP (addr)
    handlers_[0x52] = [this]() { handleEorAbsoluteIndirect(); };  // EOR (addr)
    handlers_[0xB2] = [this]() { handleLdaAbsoluteIndirect(); };  // LDA (addr)
    handlers_[0x12] = [this]() { handleOraAbsoluteIndirect(); };  // ORA (addr)
    handlers_[0xF2] = [this]() { handleSbcAbsoluteIndirect(); };  // SBC (addr)
    handlers_[0x92] = [this]() { handleStaAbsoluteIndirect(); };  // STA (addr)

    // 65C02 Jump Absolute Indexed Indirect
    handlers_[0x7C] = [this]() { handleJmpAbsoluteIndexedIndirect(); };  // JMP (addr,X)

    // 65C02 Store Zero instructions
    handlers_[0x64] = [this]() { handleStzZeroPage(); };     // STZ zp
    handlers_[0x74] = [this]() { handleStzZeroPageX(); };    // STZ zp,X
    handlers_[0x9C] = [this]() { handleStzAbsolute(); };     // STZ abs
    handlers_[0x9E] = [this]() { handleStzAbsoluteX(); };    // STZ abs,X

    // 65C02 Test and Reset/Set Bits
    handlers_[0x14] = [this]() { handleTrbZeroPage(); };     // TRB zp
    handlers_[0x1C] = [this]() { handleTrbAbsolute(); };     // TRB abs
    handlers_[0x04] = [this]() { handleTsbZeroPage(); };     // TSB zp
    handlers_[0x0C] = [this]() { handleTsbAbsolute(); };     // TSB abs

    // 65C02 Processor Control
    handlers_[0xDB] = [this]() { handleStp(); };             // STP - Stop processor
    handlers_[0xCB] = [this]() { handleWai(); };             // WAI - Wait for interrupt
}


} // namespace Computer
