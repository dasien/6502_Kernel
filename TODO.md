# TODO List

- [x] Z: & T: commands are updating the current address to 00FF and 01FF respectively and they shouldn't.
- [x] Fix BASIC token parsing (e.g. enter 10 FOR I = 1 TO 10) and that is not what prints when you LIST
- [ ] Emulator: 65C02 bare "(zp)" indirect opcodes ($12/$32/$52/$72/$92/$B2/$D2/$F2) are decoded as 2-byte absolute-indirect instead of 1-byte zero-page-indirect in CPU6502 (calculateAbsoluteIndirectAddress). Dormant: the EhBASIC ROM doesn't use these forms, so it's unexercised today, but it's a latent correctness gap for other 65C02 code.