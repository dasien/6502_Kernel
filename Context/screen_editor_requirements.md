How the PLOT Routine Tracks Screen Position:
The PLOT routine uses several zero page memory locations to track the current cursor position:

Primary Cursor Position Variables:

$D3 - Current cursor column (0-39)
$D6 - Current cursor row (0-24)

PLOT Routine Operation:
The PLOT routine works in two modes based on the carry flag:

Get Position (Carry Set): If the Carry flag is set before calling the function, then the cursor position will be returned in registers X and Y PLOT
Set Position (Carry Clear): If the Carry flag is cleared before calling the function, then the cursor will be set to the position specified by in registers X and Y

How CHROUT Tests for Column Boundaries

Column Boundary Detection: The CHROUT routine tracks the current cursor position using these key memory locations:

    $D3 - Current cursor column (0-39)
    $D6 - Current cursor row (0-24)

When CHROUT processes a character, it:

    Checks if the current column ($D3) is at position 39 (the last column)
    If at column 39, it wraps to the next line by:
        Setting $D3 to 0 (column 0)
        Incrementing $D6 (next row)
        Creating a logical line link in the screen editor's internal tables

Maximum Text Length Limits

80 Character Logical Line Limit: Programming: a line can have 80 characters maximum (40 characters over 2 lines) - any further characters are ignored

This is a critical limitation - the screen editor has a hard limit of 80 characters per logical line. This means:

Why 80 Characters? Program lines can be 80 characters total on most machines, but machines with 40 column text would cause the line to wrap around to the next line on the screen
2 line limit syntax error - Commodore 64 - Lemon64 - Commodore 64

This 80-character limit was designed to:

    Be compatible with standard computer terminals of the era
    Allow logical lines to fit exactly in 2 physical screen lines
    Maintain compatibility with systems that had 80-column displays

Memory Locations and Updates

Updated During Text Output: When CHROUT wraps text, it updates several memory areas:

    Cursor Position Variables:
        $D3 (column) - Reset to 0 when wrapping
        $D6 (row) - Incremented when wrapping
    Screen Memory:
        Characters placed in screen RAM ($0400-$07E7)
        Color attributes placed in color RAM (D800−D800−DBFF)
    Screen Editor Line Tables:
        Internal tables track which physical lines belong to the same logical line
        These tables manage the relationship between logical and physical lines

No Unlimited Text Printing

Hard Limits Apply: Unlike modern systems where you can print unlimited text, the C64 has several constraints:

    80 characters per logical line maximum
    25 physical lines on screen
    Automatic scrolling when reaching bottom-right corner

Character Processing Flow:

    CHROUT receives character in A register
    Checks current column position ($D3)
    If column < 39: places character and increments column
    If column = 39: places character, wraps to next line, resets column to 0
    Updates screen memory and color memory accordingly
    Maintains logical line linking in internal tables


