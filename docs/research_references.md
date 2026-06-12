# Research References

Working list of repos, datasheets, docs, and other sources informing the design
of upcoming features — primarily the **in-machine text editor** and **resident
filesystem** (post-v3.2). Paste links and short notes under the relevant heading.
For each entry, a one-line note on *why it's relevant* and (if known) its
**license** is helpful — license fit gates whether we can port/borrow code.

Format suggestion per entry:
```
- <url>
  - what: one line on what it is
  - relevant: why it matters for our design
  - license: e.g. MIT / GPL / BSD / unknown / docs-only
```

---

## Resident filesystem — format & 6502 implementations
(FAT12/16/32, CBM-DOS-style, or a custom format; existing 6502 FS code to study or port.)

- https://github.com/commanderx16/x16-rom/tree/master/dos/fat32
  - what: Commander X16 ROM's FAT32 implementation (6502)
  - relevant: candidate to port/adapt; reference for FAT directory walking, etc.
  - license: (to confirm)



## Storage backing — how the host presents "disk" to the emulated machine
(Single disk-image file? A host directory? An SD-card image? Block-device API the
emulator exposes vs. the current per-op host file dialogs.)
https://mike42.me/blog/2021-12-adding-an-sd-card-reader-to-my-6502-computer
https://mike42.me/blog/2021-12-implementing-the-xmodem-protocol-for-file-transfer
https://6502.org/forum/viewtopic.php?f=2&t=5824
https://github.com/x16community/x16-emulator

## Assemblers and language implementations
Examples of assembler/disassembler programs or other langage interpreters/compilers
https://github.com/Museum-of-Art-and-Digital-Entertainment/macross
https://archive.org/stream/6502MacroAssemblerAndTextEditorForPETAPPLESYM/6502%20Macro%20Assembler%20and%20Text%20Editor%20for%20PET%2C%20APPLE%2C%20SYM_djvu.txt
https://github.com/jefftranter/6502/blob/master/asm/jmon/miniasm.s
https://github.com/mike42/6502-computer/blob/main/rom/basic/basic.s
https://github.com/Klaus2m5/6502_EhBASIC_V2.22/blob/master/basic.asm
https://github.com/davervw/vwas6502
https://mike42.me/blog/2021-09-porting-basic-to-my-6502-computer


## Text editor — implementations & the "feel" we want
(Full-screen vs modal vs line-numbered; text-buffer + screen-redraw approaches.)

- https://turbo.style64.org/docs/turbo-macro-pro-editor
  - what: Turbo Macro Pro editor command reference
  - relevant: full-screen integrated editor model (the gold-standard feel)
  - license: docs-only (TMP itself: to confirm)

- https://sourceforge.net/p/vi65/code/HEAD/tree/trunk/
  - what: vi65 — a vi editor for 6502 systems
  - relevant: a standalone editor port option (modal)
  - license: (to confirm)



## Screen / cursor / terminal handling
(Our kernel I/O is a PRINT_CHAR byte stream with no gotoxy in the ABI; an editor
needs cursor addressing, insert/delete-with-reflow, and scrolling. References on
how editors drive the screen.)




## CPU / hardware datasheets & references
(WDC 65C02, timing, anything relevant to new features.)




## Misc / inspiration
(Other 6502 systems, ROM projects, blog posts, forum threads — anything loosely
relevant, e.g. the broader X16 ROM project.)
https://github.com/haldean/x6502/blob/master/cpu.h
https://github.com/mist64/c64rom/blob/master/kernal/kernal.s
https://github.com/Klaus2m5/6502_65C02_functional_tests/blob/master/6502_interrupt_test.a65
https://github.com/iScsc/6502-assembly/tree/main/src
https://c64os.com/post/c64kernalrom#scr_setmsg

## Unsorted - Please sort these into relevant sections above or create new ones.
https://github.com/davidgiven/cpm65

## Games / Assembler Programs to port
https://github.com/jefftranter/6502/tree/master/asm/KIM-1/TheFirstBookOfKIM/Games
https://www.linusakesson.net/software/zeugma/index.php
https://6502.org/source/?product=87