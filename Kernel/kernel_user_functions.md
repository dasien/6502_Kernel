# MFC 6502 Kernel API Reference

## Overview

The MFC 6502 Kernel provides a set of ROM routines that user programs can call to perform common I/O operations. These routines are accessed via JSR (Jump to Subroutine) to fixed addresses in the ROM.

## Calling Convention

All kernel routines are called using the standard 6502 JSR instruction. Unless otherwise noted:
- Parameters are passed in registers (A, X, Y)
- Return values are passed in registers
- The carry flag may be used to indicate success/failure
- Other flags may be modified

## API Reference

### Character Output

#### PRINT_CHAR - $FF00
Print a single character to the screen at the current cursor position.

- **Address:** `$FF00`
- **Input:** A = ASCII character to print
- **Output:** None
- **Modifies:** A, X, Y
- **Special:** Handles special characters:
    - $0D (CR) - Moves cursor to start of next line
    - $08 (BS) - Backspace (moves cursor back and clears character)

**Example:**
```assembly
    LDA #'A'        ; Load ASCII 'A'
    JSR $FF00       ; Print it
    
    LDA #$0D        ; Print carriage return
    JSR $FF00       ; Moves to next line
```

---

#### PRINT_MESSAGE - $FF03
Print a null-terminated string to the screen.

- **Address:** `$FF03`
- **Input:** Message address must be stored in zero page locations:
  - `$04` (MON_MSG_PTR_LO) = Low byte of string address
  - `$05` (MON_MSG_PTR_HI) = High byte of string address
- **Output:** None
- **Modifies:** A, Y
- **Limitations:** Strings must be < 256 characters

**Example:**
```assembly
    ; Print "HELLO WORLD" message
    LDA #<HELLO_MSG     ; Load low byte of message address
    STA $04             ; Store in message pointer low
    LDA #>HELLO_MSG     ; Load high byte of message address  
    STA $05             ; Store in message pointer high
    JSR $FF03           ; Print the message

HELLO_MSG:
    .BYTE "HELLO WORLD", 0  ; Null-terminated string
```

---

#### PRINT_NEWLINE - $FF06
Print a carriage return (newline) to move cursor to start of next line.

- **Address:** `$FF06`
- **Input:** None
- **Output:** None
- **Modifies:** A, X, Y

**Example:**
```assembly
    JSR $FF06       ; Move to next line
```

---

#### GET_KEY - $FF09  
Wait for and return a keystroke from the keyboard.

- **Address:** `$FF09`
- **Input:** None
- **Output:** A = ASCII character code of pressed key
- **Modifies:** A
- **Behavior:** Blocks until a key is pressed

**Example:**
```assembly
    JSR $FF09       ; Wait for keypress
    CMP #$0D        ; Check if Enter was pressed
    BEQ ENTER_PRESSED
    ; Handle other keys...
ENTER_PRESSED:
    ; Handle Enter key
```

---

#### CLEAR_SCREEN - $FF0C
Clear the entire screen and reset cursor to top-left position.

- **Address:** `$FF0C`
- **Input:** None
- **Output:** None  
- **Modifies:** A, X
- **Effect:** Fills screen with space characters ($20)

**Example:**
```assembly
    JSR $FF0C       ; Clear screen and reset cursor

 