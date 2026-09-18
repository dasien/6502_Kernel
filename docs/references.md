# External References

A working bibliography gathered while designing the in-machine editor and the resident
filesystem. Both have since shipped, so this is research history rather than a live
list. It is kept because the licence notes are what decide whether we may port or
borrow code, and the README's acknowledgments depend on them.

---

This was a working list of repositories, datasheets, documents and other sources
informing the design of what were then upcoming features, chiefly the in-machine text
editor and the resident filesystem. Links and short notes go under the relevant
heading. Each entry is most useful with a one-line note on why it matters and, where
it is known, its licence, because licence fit is what decides whether we can port or
borrow the code at all.

Format suggestion per entry:
```
- <url>
  - what: one line on what it is
  - relevant: why it matters for our design
  - license: e.g. MIT / GPL / BSD / unknown / docs-only
```

---

### Resident filesystem — format & 6502 implementations
This covers FAT12, FAT16 and FAT32, CBM-DOS-style formats and custom ones, along with existing 6502 filesystem code worth studying or porting.

- https://github.com/commanderx16/x16-rom/tree/master/dos/fat32
  - what: Commander X16 ROM's FAT32 implementation (6502)
  - relevant: candidate to port/adapt; reference for FAT directory walking, etc.
  - license: (to confirm)



### Storage backing — how the host presents "disk" to the emulated machine
(Single disk-image file? A host directory? An SD-card image? Block-device API the
emulator exposes vs. the current per-op host file dialogs.)
https://mike42.me/blog/2021-12-adding-an-sd-card-reader-to-my-6502-computer
https://mike42.me/blog/2021-12-implementing-the-xmodem-protocol-for-file-transfer
https://6502.org/forum/viewtopic.php?f=2&t=5824
https://github.com/x16community/x16-emulator

### Assemblers and language implementations
Examples of assembler/disassembler programs or other langage interpreters/compilers
https://github.com/Museum-of-Art-and-Digital-Entertainment/macross
https://archive.org/stream/6502MacroAssemblerAndTextEditorForPETAPPLESYM/6502%20Macro%20Assembler%20and%20Text%20Editor%20for%20PET%2C%20APPLE%2C%20SYM_djvu.txt
https://github.com/jefftranter/6502/blob/master/asm/jmon/miniasm.s
https://github.com/mike42/6502-computer/blob/main/rom/basic/basic.s
https://github.com/Klaus2m5/6502_EhBASIC_V2.22/blob/master/basic.asm
https://github.com/davervw/vwas6502
https://mike42.me/blog/2021-09-porting-basic-to-my-6502-computer


### Text editor — implementations & the "feel" we want
This covers full-screen against modal against line-numbered designs, and the text-buffer and screen-redraw approaches behind them.

- https://turbo.style64.org/docs/turbo-macro-pro-editor
  - what: Turbo Macro Pro editor command reference
  - relevant: full-screen integrated editor model (the gold-standard feel)
  - license: docs-only (TMP itself: to confirm)

- https://sourceforge.net/p/vi65/code/HEAD/tree/trunk/
  - what: vi65 — a vi editor for 6502 systems
  - relevant: a standalone editor port option (modal)
  - license: (to confirm)



### Screen / cursor / terminal handling
Our kernel I/O is a `PRINT_CHAR` byte stream with no gotoxy in the ABI, whereas an
editor needs cursor addressing, insert and delete with reflow, and scrolling. The
references here cover
how editors drive the screen.)




### CPU / hardware datasheets & references
(WDC 65C02, timing, anything relevant to new features.)




### Misc / inspiration
Other 6502 systems, ROM projects, blog posts and forum threads go here. Anything
loosely relevant qualifies, such as the broader X16 ROM project.
https://github.com/haldean/x6502/blob/master/cpu.h
https://github.com/mist64/c64rom/blob/master/kernal/kernal.s
https://github.com/Klaus2m5/6502_65C02_functional_tests/blob/master/6502_interrupt_test.a65
https://github.com/iScsc/6502-assembly/tree/main/src
https://c64os.com/post/c64kernalrom#scr_setmsg

### Unsorted - Please sort these into relevant sections above or create new ones.
https://github.com/davidgiven/cpm65

### Sound — MOS 6581/8580 SID
Datasheets and reverse-engineering notes behind the software SID. These are
documentation and behaviour references only. The synthesiser was written from scratch,
and reSID was deliberately not ported because it is GPL.
- SID 6581/8580 datasheet (register map, waveforms, ADSR rate tables, filter)
  - what: the chip our `SID` model targets
  - relevant: register layout relocated to `$FE38`; envelope/filter behavior
  - license: docs-only
- reSID (Dag Lem), studied for behaviour and deliberately not copied
  - relevant: reference for combined-waveform / filter nonlinearity notes
  - license: GPL — do not port code; behavior reference only

### Games / Assembler Programs to port
https://github.com/jefftranter/6502/tree/master/asm/KIM-1/TheFirstBookOfKIM/Games
https://www.linusakesson.net/software/zeugma/index.php
https://6502.org/source/?product=87
