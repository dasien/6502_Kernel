; ================================================================
; dos.asm - MFC-DOS resident ROM ($9000-$AFFF, always mapped)
; ================================================================
; The resident operating system for MFC-DOS. Eventually holds the FAT16
; filesystem driver and the DOS command shell (CCP); see docs/dos_design.md.
;
; This region is always mapped by the emulator (it is NOT in the bankable
; $B000-$DFFF module window), so its routines are reachable at all times -
; the kernel BIOS reaches the filesystem here through the $FF00 ABI.
;
; STEP 1 (memory-map shift): this is a skeleton only - just a signature at
; $9000 so the region is real ROM and the new memory map is verifiable.
; The block-device sector primitive, the FAT16 driver, and the FS ABI land
; in the following steps.
; ================================================================

.PC02                                   ; WDC 65C02 instruction set

.segment "CODE"

; ----------------------------------------------------------------
; DOS_SIGNATURE - first bytes of the ROM (at $9000)
; ----------------------------------------------------------------
; A recognizable marker so the emulator/tests can confirm the DOS ROM is
; mapped at $9000, and so a future loader can sanity-check the image.
DOS_SIGNATURE:
    .BYTE "MFC-DOS", $00
DOS_VERSION:
    .BYTE $00, $01                      ; version 0.1 (major, minor)

; Reserved for the FAT16 filesystem driver, FS ABI, and DOS shell.
