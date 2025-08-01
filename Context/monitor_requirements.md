# Monitor Modes/Commands

The monitor should have 4 major modes.
- Command mode, where the user can enter any of the commands below.
- Write mode, where the user is writing data to memory
- Read mode, where the user is reading data from memory
- Run mode, where the user wishes to execute a program
- Load mode, where the user wishes to load a assembled binary into a memory location

To switch between modes, the user types "X:".  This puts them back in Command mode.

## Write Mode

Pressing "W:xxxx" puts the monitor into write mode at that address (xxxx), assuming it is valid
    - The monitor should set xxxx as the active current address
    - If the user types "W:xxxx bb" where "bb" represents a byte value, the monitor should go ahead and write that value to the location
      after first displaying the current value at that address
    - The monitor should first display the current byte of data at that address
    - Any input after the "W:xxxx" command should be assumed to be inserting a byte of data in sequential memory addresses.
      For example, "W:8000" <Enter> and then "A9 77 56 BB FF" should write "A9" to location 8000, "77" to 8001 and so forth.
    - Once the user presses enter, the monitor should show the old values of the address range, modify the memory locations with
      the entered data, and then show the new values.  For example, if in the example above, the current bytes starting at 8000
      had the values "A0 FF FF FF A0" before editing, then the after the "W:8000" command the monitor should display "8000: A0".
      After the user enters their data, the monitor should show "8000: A0 FF FF FF A0".  The monitor should then write those bytes
       to the memory locations and then display a new line that reads, "8000: "A9 77 56 BB FF".
    - The new active current address should be the last address written to with data.
    - To exit this mode, the user should type "X:"

## Read Mode
Pressing "R:xxxx" puts the monitor into read mode at that address (xxxx), assuming it is valid.
    - The monitor should set xxxx as the active current address
    - The monitor should display the value at that address. For example, if the user types "R:8000" the monitor should display
      "8000: bb" where bb is the byte value at 8000.
    - if the user types a range by typing "R:xxxx-yyyy", the system should print the address followed by the bytes.  If the range
    requested is more than 8 bytes, the system should print another line with the address and the next 8 bytes.
    - The active current address should be the xxxx address.
    - To exit this mode, the user should type "X:"

## Run Mode

Pressing "G:xxxx" will start running a program located at "xxxx".  At thig point, the monitor program has given control over
to the user program.  It is up to the program developer to exit and call the monitor program again.  Otherwise, the computer will
need to be reset and the kernel/monitor program run.

## Load Mode
Pressing L:xxxx" will put the monitor into load mode.  In this mode, the monitor expects 'xxxx' to be a valid address in memory
and it should display a prompt where the user enters a file name for an assembled 6502 binary file (with .bin or .hex extension) which
will be loaded as if from a disk/tape peripheral via the PIA.  The monitor should report the number of bytes loaded and 'OK' if
the load succeeded or an error message if not.

## Other Commands

    - K: will clear the screen (Klear)
    - S: will print the contents of the stack memory locations to the screen (0100-01ff)
    - Z: will print the contents of zero page memory locations to the screen (0000-00ff)
    - T: will print the current address for read/writes and the byte value at that address (Target)
    - W: will put the monitor into write mode at the current address
    - R: will put the monitor into read mode at the current address
    - H: will list the available commands


## Prompt Construction

