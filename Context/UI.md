 # 6502 Monitor UI Layout - Status Sidebar (IMPLEMENTED)

## Current printStatus() Console Output
```
  Current Byte: 0xXX
CPU Status:
  A: 0xXX
  X: 0xXX  
  Y: 0xXX
  PC: 0xXXXX
  SP: 0xXX
  P: 0xXX  <- Replace with individual flag bits
  Cycles: XXXXX  <- Already in status bar, remove
```

## Current UI Layout (52x25 Characters - Wider Window)

```
+----------------------------------------+----------+
|                                        |   CPU    | Row 0
|                                        |   0x4C   | Row 1 - Current Byte
|                                        +----------+
|                                        | A: 0x00  | Row 2
|                                        | X: 0x00  | Row 3  
|         6502 Monitor Display           | Y: 0x00  | Row 4
|         (Full 40 characters)           |PC: 8000  | Row 5
|       *** DIRECT INPUT ENABLED ***     |SP: 0xFF  | Row 6
|     Click display to focus, type       +----------+
|     commands directly on screen        |NV-BDIZC  | Row 7 - Flag names
|                                        |01010001  | Row 8 - Flag values
|                                        |          | Row 9
|                                        |          | Row 10
|                                        |          | Rows 11-22
|                                        |          | (Reserved)
+----------------------------------------+----------+
```

## Status Sidebar Layout (Columns 41-51, 10 chars wide)

### Section 1: Current State (Rows 0-1)
```
|   CPU    |  - Header
|   0x4C   |  - Current Byte value (mem_.read(reg.PC))
```

### Section 2: Registers (Rows 2-6)  
```
| A: 0x00  |  - Accumulator
| X: 0x00  |  - X Register
| Y: 0x00  |  - Y Register  
|PC: 8000  |  - Program Counter (4 hex digits)
|SP: 0xFF  |  - Stack Pointer
```

### Section 3: Processor Flags (Rows 7-8)
```
|NV-BDIZC  |  - Flag bit names (N=Negative, V=oVerflow, B=Break, D=Decimal, I=Interrupt, Z=Zero, C=Carry)
|01010001  |  - Current flag values (0=clear, 1=set)
```

## 6502 Processor Status Flag Mapping
```
Bit 7: N (Negative)
Bit 6: V (Overflow) 
Bit 5: - (Unused, always 1)
Bit 4: B (Break)
Bit 3: D (Decimal)
Bit 2: I (Interrupt)
Bit 1: Z (Zero)
Bit 0: C (Carry)
```

## Implementation Status - COMPLETE

### Window/UI Framework Changes ✓
- Window width expanded to 52 characters (40 + 12 for sidebar and separator)
- All existing 6502 screen memory at 40x25 preserved unchanged
- Status sidebar renders outside the 6502 screen memory area

### Direct Input Implementation ✓
- **DisplayWidget Enhanced**: Now accepts keyboard input directly
- **QLineEdit Removed**: No separate input field needed
- **Focus-based Input**: Click display to focus, type commands directly
- **Cursor Display**: Blinking cursor appears when display has focus
- **Complete Key Support**: Letters, numbers, Enter, Backspace, special keys

### Console Output Replacement ✓
- `printStatus()` updates sidebar labels instead of console output
- Status sidebar rendered by UI framework, not 6502 assembly
- Fixed position labels that don't scroll
- Individual flag bits displayed instead of hex P register value
- All console debug output removed for performance

### Direct Input System (Implemented)
```cpp
// DisplayWidget enhanced with keyboard input
class DisplayWidget : public QWidget {
    Q_OBJECT
signals:
    void keyPressed(uint8_t ascii_code);  // Emits on any key press
    
protected:
    void keyPressEvent(QKeyEvent* event) override;      // Handles Qt key events
    void focusInEvent(QFocusEvent* event) override;     // Shows cursor
    void focusOutEvent(QFocusEvent* event) override;    // Hides cursor
    
private:
    uint8_t qtKeyToAscii(QKeyEvent* event) const;       // Qt key → ASCII conversion
    void drawCursor(QPainter& painter);                 // Draws blinking cursor
    void blinkCursor();                                 // Timer-based cursor blink
};

// MainWindow connection
void onDisplayKeyPressed(uint8_t ascii_code) {
    computer_->getPia()->addKeypress(ascii_code);       // Direct to PIA system
}
```

### Status Update Functions (Implemented)
```cpp
void updateCpuStatusSidebar();    // Single consolidated function updates all:
                                  // - CPU header, current byte, registers
                                  // - Individual processor flag bits
                                  // - Real-time updates via QTimer
```

### Display Area Implementation ✓
- **6502 monitor display**: Full 40 characters (columns 0-39) preserved
- **Status sidebar**: Columns 41-51 (10 chars wide) implemented
- **Vertical separator**: Column 40 visual separation
- **Assembly code**: No changes to existing PRINT_CHAR or screen positioning
- **Direct input flow**: DisplayWidget → PIA → kernel.rom → screen display
- **Focus management**: Click display to enable keyboard input
- **Visual feedback**: Blinking cursor indicates input focus

## Benefits - ALL ACHIEVED ✓
- **Real-time CPU state visibility**: Status sidebar updates 10x/second
- **Individual flag bit status**: NV-BDIZC format with 0/1 values
- **No console output**: All debug output removed for performance
- **No assembly changes**: All existing 6502 code preserved exactly
- **Direct input experience**: Type commands directly on authentic 6502 screen
- **Focus-based UI**: Click display to input, visual cursor feedback
- **Complete key support**: Monitor commands (W:, R:, G:, etc.) work perfectly
- **Authentic terminal feel**: True 6502 computer experience
- **Performance optimized**: Console I/O bottleneck eliminated

## Current User Experience
1. **Power On**: Click "Power On" button
2. **Focus Display**: Click the 6502 screen area
3. **Direct Input**: Type monitor commands directly (W:8000 FF, R:8000, etc.)
4. **Real-time Feedback**: CPU status updates live in sidebar
5. **Authentic Feel**: Genuine 6502 terminal experience