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

void CPU6502::setFlag(StatusFlags flag, bool value)
{
    if (value)
    {
        reg.P |= flag;
    } else
    {
        reg.P &= ~flag;
    }
}

bool CPU6502::getFlag(StatusFlags flag) const
{
    return (reg.P & flag) != 0;
}

void CPU6502::updateZeroNegativeFlags(uint8_t value)
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
    uint16_t word = mem_.readWord(reg.PC);
    reg.PC += 2;
    cycles_ += 2;
    return word;
}

void CPU6502::pushByte(uint8_t value)
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
    uint8_t opcode = readByte();
    
    auto it = handlers_.find(opcode);
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
std::pair<uint16_t, bool> CPU6502::calculateAddress(bool use_one_byte, uint8_t offset)
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
        uint16_t base_address = mem_.readWord(reg.PC);
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

uint16_t CPU6502::calculateAddressSimple(bool use_one_byte, uint8_t offset)
{
    // Wrapper for backward compatibility - returns just the address
    auto [address, page_crossed] = calculateAddress(use_one_byte, offset);
    return address;
}

std::pair<uint16_t, bool> CPU6502::calculateRelativeAddress(uint8_t offset)
{
    // Calculate the new PC after the branch
    uint16_t current_pc = reg.PC; // PC already points to instruction after branch

    // Convert signed offset (if > 127, it's negative)
    int8_t signed_offset = (offset < 0x80) ? offset : offset - 0x100;

    // Calculate target address
    uint16_t target_pc = current_pc + signed_offset;

    // Check if we crossed a page boundary
    bool page_crossed = checkPageBoundaryCrossed(current_pc, target_pc);

    return std::make_pair(target_pc, page_crossed);
}

std::pair<uint16_t, uint8_t> CPU6502::calculateIndexedAddress(uint8_t offset)
{
    // This is for indexed indirect addressing (zp,X)
    // No additional cycles for page boundary crossing in this mode
    uint8_t addcycle = 0;

    // Get the zero page address from PC and add offset
    uint8_t zp_addr = mem_.read(reg.PC);
    uint8_t zp_final = (zp_addr + offset) & 0xFF;

    // Read the 16-bit address from (zp+X) and (zp+X+1)
    uint16_t address = mem_.read(zp_final) | (mem_.read((zp_final + 1) & 0xFF) << 8);

    // Validate address
    validateAddress(address);

    return std::make_pair(address, addcycle);
}

std::pair<uint16_t, uint8_t> CPU6502::calculateIndirectAddress(uint8_t offset)
{
    // This holds any additional cycle timing due to page boundary crossing
    uint8_t addcycle = 0;

    // Get the zero page address from PC
    uint8_t zp_addr = mem_.read(reg.PC) & 0xFF;

    // Read the 16-bit base address from (zp) and (zp+1)
    uint16_t base_address = mem_.read(zp_addr) | (mem_.read((zp_addr + 1) & 0xFF) << 8);

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

bool CPU6502::checkPageBoundaryCrossed(uint16_t base_addr, uint16_t final_addr)
{
    // Check if addresses are on different pages (different high bytes)
    return (base_addr & 0xFF00) != (final_addr & 0xFF00);
}

bool CPU6502::validateAddress(uint16_t address)
{
    if (address > 0xFFFF)
    {
        // Invalid address accessed
        return false;
    }
    return true;
}

// ALU helper functions
uint8_t CPU6502::addValues(uint8_t val1, uint8_t val2)
{
    // Do the math A+M+C
    uint16_t result = val1 + val2 + (getFlag(kCarry) ? 1 : 0);

    // Check to see if this is BCD mode
    if (getFlag(kDecimal))
    {
        // Do the conversion to BCD
        result = convertToBcd(result);

        // Now check to see if we need to set carry
        setFlag(kCarry, (result > 0x99));
    } else
    {
        // Set carry and overflow based on result
        setFlag(kCarry, (result > 0xFF));
        setFlag(kOverflow, (val1 < 128 && val2 < 128 && result > 127));
    }

    // Return the 8-bit result
    return result & 0xFF;
}

uint8_t CPU6502::subtractValues(uint8_t val1, uint8_t val2)
{
    // Do the math A-M-(1-C)
    int16_t result = val1 - val2 - (getFlag(kCarry) ? 0 : 1);

    // Check to see if this is BCD mode
    if (getFlag(kDecimal))
    {
        // Do the conversion to BCD
        result = convertToBcd(result);

        // Now check to see if we need to set carry
        setFlag(kCarry, (result > 0x99));
    } else
    {
        // Set carry and overflow based on result
        setFlag(kCarry, (result >= 0));
        setFlag(kOverflow, (val1 < 128 && val2 < 128 && result > 127));
    }

    // Return the 8-bit result
    return result & 0xFF;
}

void CPU6502::compareValues(uint8_t val1, uint8_t val2)
{
    // Set flags
    setFlag(kCarry, (val1 >= val2));
    setFlag(kZero, (val1 == val2));
    setFlag(kNegative, (val1 & 0x80));
}

uint8_t CPU6502::convertToBcd(uint8_t value)
{
    // Mask LSBit to see if that digit is greater than 9
    if ((value & 0x0F) > 0x09)
    {
        // Roll the digit
        value += 0x06;
    }

    // Mask MSBit to see if that digit is greater than 9
    if ((value & 0xF0) > 0x90)
    {
        // Roll that digit
        value += 0x60;
    }

    return value;
}

// Stack helper functions
void CPU6502::pushStack16(uint16_t value)
{
    // Push the high byte first (6502 pushes MSB first)
    pushByte((value >> 8) & 0xFF);
    
    // Push the low byte second
    pushByte(value & 0xFF);
}

uint16_t CPU6502::popStack16()
{
    // Pull low byte first (6502 pulls LSB first)
    uint8_t low_byte = pullByte();
    
    // Pull high byte second
    uint8_t high_byte = pullByte();
    
    return (high_byte << 8) | low_byte;
}

// ADC instruction family handlers
void CPU6502::handleAdcImmediate()
{
    handleAdcBase(reg.PC, 1, 2);
}

void CPU6502::handleAdcZeroPage()
{
    uint16_t address = calculateAddressSimple(true, 0);
    handleAdcBase(address, 1, 3);
}

void CPU6502::handleAdcZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleAdcBase(address, 1, 4);
}

void CPU6502::handleAdcAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleAdcBase(address, 2, 4);
}

void CPU6502::handleAdcAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleAdcBase(address, 2, cycles);
}

void CPU6502::handleAdcAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
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
    uint8_t cycles = 5 + cycle;
    handleAdcBase(address, 1, cycles);
}

void CPU6502::handleAdcBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleAndBase(address, 1, 3);
}

void CPU6502::handleAndZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleAndBase(address, 1, 4);
}

void CPU6502::handleAndAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleAndBase(address, 2, 4);
}

void CPU6502::handleAndAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleAndBase(address, 2, cycles);
}

void CPU6502::handleAndAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
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
    uint8_t cycles = 5 + cycle;
    handleAndBase(address, 1, cycles);
}

void CPU6502::handleAndBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleAslBase(address, 1, 5);
}

void CPU6502::handleAslZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleAslBase(address, 1, 6);
}

void CPU6502::handleAslAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleAslBase(address, 2, 6);
}

void CPU6502::handleAslAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleAslBase(address, 2, 7);
}

void CPU6502::handleAslBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
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
void CPU6502::handleBcc()
{
    // Branch if Carry Clear
    uint8_t offset = readByte();
    
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
    uint8_t offset = readByte();
    
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
    uint8_t offset = readByte();
    
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
    uint8_t offset = readByte();
    
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
    uint8_t offset = readByte();
    
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
    uint8_t offset = readByte();
    
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
    uint8_t offset = readByte();
    
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
    uint8_t offset = readByte();
    
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleLdaBase(address, 1, 3);
}

void CPU6502::handleLdaZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleLdaBase(address, 1, 4);
}

void CPU6502::handleLdaAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleLdaBase(address, 2, 4);
}

void CPU6502::handleLdaAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleLdaBase(address, 2, cycles);
}

void CPU6502::handleLdaAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
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
    uint8_t cycles = 5 + cycle;
    handleLdaBase(address, 1, cycles);
}

void CPU6502::handleLdaBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    reg.A = val;
    updateZeroNegativeFlags(reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// JMP instruction handlers
void CPU6502::handleJmpAbsolute()
{
    handleJmpBase(readWord(), 0, 3);
}

void CPU6502::handleJmpIndirect()
{
    uint16_t indirect_addr = readWord();
    uint16_t target_addr = mem_.readWord(indirect_addr);
    handleJmpBase(target_addr, 0, 5);
}

void CPU6502::handleJmpBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    reg.PC = address;
    cycles_ += cycles;
}

// STA instruction family handlers
void CPU6502::handleStaZeroPage()
{
    uint16_t address = calculateAddressSimple(true, 0);
    handleStaBase(address, 1, 3);
}

void CPU6502::handleStaZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleStaBase(address, 1, 4);
}

void CPU6502::handleStaAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
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

void CPU6502::handleStaBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    mem_.write(address, reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// JSR instruction handler
void CPU6502::handleJsr()
{
    // Calculate return address before reading target (PC currently points to low byte of target)
    uint16_t return_address = reg.PC + 1; // Address of last byte of JSR instruction
    
    // Read target address (this advances PC by 2)
    uint16_t target_address = readWord();
    
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
    uint16_t return_address = popStack16();
    
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleLdxBase(address, 1, 3);
}

void CPU6502::handleLdxZeroPageY()
{
    uint16_t address = calculateAddressSimple(true, reg.Y);
    handleLdxBase(address, 1, 4);
}

void CPU6502::handleLdxAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleLdxBase(address, 2, 4);
}

void CPU6502::handleLdxAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleLdxBase(address, 2, cycles);
}

void CPU6502::handleLdxBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleLdyBase(address, 1, 3);
}

void CPU6502::handleLdyZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleLdyBase(address, 1, 4);
}

void CPU6502::handleLdyAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleLdyBase(address, 2, 4);
}

void CPU6502::handleLdyAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleLdyBase(address, 2, cycles);
}

void CPU6502::handleLdyBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    reg.Y = val;
    updateZeroNegativeFlags(reg.Y);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// STX instruction family handlers
void CPU6502::handleStxZeroPage()
{
    uint16_t address = calculateAddressSimple(true, 0);
    handleStxBase(address, 1, 3);
}

void CPU6502::handleStxZeroPageY()
{
    uint16_t address = calculateAddressSimple(true, reg.Y);
    handleStxBase(address, 1, 4);
}

void CPU6502::handleStxAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleStxBase(address, 2, 4);
}

void CPU6502::handleStxBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    mem_.write(address, reg.X);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// STY instruction family handlers
void CPU6502::handleStyZeroPage()
{
    uint16_t address = calculateAddressSimple(true, 0);
    handleStyBase(address, 1, 3);
}

void CPU6502::handleStyZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleStyBase(address, 1, 4);
}

void CPU6502::handleStyAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleStyBase(address, 2, 4);
}

void CPU6502::handleStyBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleCmpBase(address, 1, 3);
}

void CPU6502::handleCmpZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleCmpBase(address, 1, 4);
}

void CPU6502::handleCmpAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleCmpBase(address, 2, 4);
}

void CPU6502::handleCmpAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleCmpBase(address, 2, cycles);
}

void CPU6502::handleCmpAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
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
    uint8_t cycles = 5 + cycle;
    handleCmpBase(address, 1, cycles);
}

void CPU6502::handleCmpBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleCpxBase(address, 1, 3);
}

void CPU6502::handleCpxAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleCpxBase(address, 2, 4);
}

void CPU6502::handleCpxBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleCpyBase(address, 1, 3);
}

void CPU6502::handleCpyAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleCpyBase(address, 2, 4);
}

void CPU6502::handleCpyBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleSbcBase(address, 1, 3);
}

void CPU6502::handleSbcZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleSbcBase(address, 1, 4);
}

void CPU6502::handleSbcAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleSbcBase(address, 2, 4);
}

void CPU6502::handleSbcAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleSbcBase(address, 2, cycles);
}

void CPU6502::handleSbcAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
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
    uint8_t cycles = 5 + cycle;
    handleSbcBase(address, 1, cycles);
}

void CPU6502::handleSbcBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleEorBase(address, 1, 3);
}

void CPU6502::handleEorZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleEorBase(address, 1, 4);
}

void CPU6502::handleEorAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleEorBase(address, 2, 4);
}

void CPU6502::handleEorAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleEorBase(address, 2, cycles);
}

void CPU6502::handleEorAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
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
    uint8_t cycles = 5 + cycle;
    handleEorBase(address, 1, cycles);
}

void CPU6502::handleEorBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleOraBase(address, 1, 3);
}

void CPU6502::handleOraZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleOraBase(address, 1, 4);
}

void CPU6502::handleOraAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleOraBase(address, 2, 4);
}

void CPU6502::handleOraAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
    handleOraBase(address, 2, cycles);
}

void CPU6502::handleOraAbsoluteY()
{
    auto [address, page_crossed] = calculateAddress(false, reg.Y);
    uint8_t cycles = 4 + (page_crossed ? 1 : 0);
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
    uint8_t cycles = 5 + cycle;
    handleOraBase(address, 1, cycles);
}

void CPU6502::handleOraBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    reg.A |= val;  // Logical OR operation
    updateZeroNegativeFlags(reg.A);
    reg.PC += pc_offset;
    cycles_ += cycles;
}

// BIT instruction family handlers
void CPU6502::handleBitZeroPage()
{
    uint16_t address = calculateAddressSimple(true, 0);
    handleBitBase(address, 1, 3);
}

void CPU6502::handleBitAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleBitBase(address, 2, 4);
}

void CPU6502::handleBitBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    uint8_t result = reg.A & val;
    
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleLsrBase(address, 1, 5);
}

void CPU6502::handleLsrZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleLsrBase(address, 1, 6);
}

void CPU6502::handleLsrAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleLsrBase(address, 2, 6);
}

void CPU6502::handleLsrAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleLsrBase(address, 2, 7);
}

void CPU6502::handleLsrBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
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
    bool old_carry = getFlag(kCarry);
    
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleRolBase(address, 1, 5);
}

void CPU6502::handleRolZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleRolBase(address, 1, 6);
}

void CPU6502::handleRolAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleRolBase(address, 2, 6);
}

void CPU6502::handleRolAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleRolBase(address, 2, 7);
}

void CPU6502::handleRolBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    bool old_carry = getFlag(kCarry);
    
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
    bool old_carry = getFlag(kCarry);
    
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleRorBase(address, 1, 5);
}

void CPU6502::handleRorZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleRorBase(address, 1, 6);
}

void CPU6502::handleRorAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleRorBase(address, 2, 6);
}

void CPU6502::handleRorAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleRorBase(address, 2, 7);
}

void CPU6502::handleRorBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
{
    uint8_t val = mem_.read(address);
    bool old_carry = getFlag(kCarry);
    
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleIncBase(address, 1, 5);
}

void CPU6502::handleIncZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleIncBase(address, 1, 6);
}

void CPU6502::handleIncAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleIncBase(address, 2, 6);
}

void CPU6502::handleIncAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleIncBase(address, 2, 7);
}

void CPU6502::handleIncBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
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
    uint16_t address = calculateAddressSimple(true, 0);
    handleDecBase(address, 1, 5);
}

void CPU6502::handleDecZeroPageX()
{
    uint16_t address = calculateAddressSimple(true, reg.X);
    handleDecBase(address, 1, 6);
}

void CPU6502::handleDecAbsolute()
{
    uint16_t address = calculateAddressSimple(false, 0);
    handleDecBase(address, 2, 6);
}

void CPU6502::handleDecAbsoluteX()
{
    auto [address, page_crossed] = calculateAddress(false, reg.X);
    handleDecBase(address, 2, 7);
}

void CPU6502::handleDecBase(uint16_t address, uint8_t pc_offset, uint8_t cycles)
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
    
    // Transfer instructions
    handlers_[0xAA] = [this]() { handleTax(); };
    handlers_[0xA8] = [this]() { handleTay(); };
    handlers_[0xBA] = [this]() { handleTsx(); };
    handlers_[0x8A] = [this]() { handleTxa(); };
    handlers_[0x9A] = [this]() { handleTxs(); };
    handlers_[0x98] = [this]() { handleTya(); };
}


} // namespace Computer
