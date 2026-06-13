; ================================================================
; dos.asm - MFC-DOS resident ROM ($9000-$AFFF, always mapped)
; ================================================================
; The resident operating system for MFC-DOS: the FAT16 filesystem driver and
; (later) the DOS command shell. See docs/dos_design.md.
;
; This region is always mapped by the emulator (it is NOT in the bankable
; $B000-$DFFF module window), so its routines are reachable at all times. The
; stable entry points live in a jump table at $AF00 (the "DOS ABI"), mirroring
; the kernel's own $FF00 table - callers bind to those fixed addresses, never to
; the moving internals below.
;
; STEP 2.2 (this commit): block-device equates, the 512-byte sector read/write
; primitives (the 6502 side of the $FE24-$FE28 registers), and FS ABI stubs.
; The FAT16 mount / directory walk / cluster-chain read fill in the FS_* stubs
; in step 2.3; the DOS shell cold entry is wired in phase 4.
; ================================================================

.PC02                                   ; WDC 65C02 instruction set

; ----------------------------------------------------------------
; Block-device registers (I/O page, just past MODULE_BANK $FE23)
; ----------------------------------------------------------------
BLK_LBA          = $FE24                ; 16-bit sector number ($FE24 lo / $FE25 hi)
BLK_CMD          = $FE26                ; write: command code
BLK_STATUS       = $FE27                ; read: 0 = ready, non-zero = error
BLK_DATA         = $FE28                ; 512-byte sector data port (auto-incrementing)

BLK_CMD_READ     = $01                  ; read sector -> device buffer
BLK_CMD_WRITE    = $02                  ; device buffer -> sector
BLK_READY        = $00                  ; BLK_STATUS: ready / last op OK

SECTOR_SIZE      = 512

; ----------------------------------------------------------------
; DOS zero page (the free $3A-$5A gap; clear of monitor $14-$39,
; BASIC $00-$13/$5B-$FF, and dev-tools $C0-$DF)
; ----------------------------------------------------------------
BLK_BUF_PTR      = $3A                  ; $3A-$3B: caller's 512-byte sector buffer
DOS_PTR          = $3C                  ; $3C-$3D: filename pointer (FS_OPEN)

; ----------------------------------------------------------------
; DOS filesystem state block ($0300-$034F)
; ----------------------------------------------------------------
; A resident scratch/state area for the FAT16 driver. $0300-$03FF is unused by
; the kernel, BASIC, and dev-tools, so it is safe across every FS caller. The
; driver streams sectors through the block device's own buffer (no 512-byte RAM
; sector buffer), so this block only holds mount info, cursors, and the current
; 32-byte directory entry.
DOS_MOUNTED      = $0300                ; 0 = not mounted, 1 = mounted
DOS_SEC_PER_CLUS = $0301                ; sectors per cluster
DOS_NUMFATS      = $0302                ; number of FATs
DOS_FATSIZE      = $0303                ; word: sectors per FAT
DOS_FAT_START    = $0305                ; word: first FAT sector (LBA)
DOS_ROOT_START   = $0307                ; word: root directory first sector (LBA)
DOS_DATA_START   = $0309                ; word: first data sector (LBA)
DOS_ROOT_ENTS    = $030B                ; word: root directory entry count
; directory enumeration cursor
DOS_DIR_LBA      = $030D                ; word: current directory sector (LBA)
DOS_DIR_IDX      = $030F                ; entry index within the sector (0-15)
DOS_DIR_LEFT     = $0310                ; word: root entries remaining
; open-file state (used in step 2.3b)
DOS_F_CLUS       = $0312                ; word: current cluster
DOS_F_LBA        = $0314                ; word: current data sector (LBA)
DOS_F_SIC        = $0316                ; sector index within the cluster
DOS_F_OFF        = $0317                ; word: byte offset within the sector (0-511)
DOS_F_LEFT       = $0319                ; 4 bytes: file bytes remaining ($0319-$031C)
; scratch + buffers
DOS_TMP          = $031D                ; word: general 16-bit scratch
DOS_TMP2         = $031F                ; word: general 16-bit scratch
DOS_ENTRY        = $0320                ; 32-byte current directory entry ($0320-$033F)
DOS_NAME83       = $0340                ; 11-byte 8.3 match buffer ($0340-$034A)

; FAT16 directory-entry field offsets (within DOS_ENTRY / on disk)
DIR_NAME         = $00                  ; 11 bytes: 8.3 name (space padded)
DIR_ATTR         = $0B                  ; attribute byte
DIR_CLUSTER_LO   = $1A                  ; word: first cluster (low; high is 0 on FAT16)
DIR_SIZE         = $1C                  ; 4 bytes: file size

ATTR_LFN         = $0F                  ; (attr & $0F)==$0F -> long-file-name entry
ATTR_VOLUME      = $08                  ; volume-label bit
DIRENT_END       = $00                  ; name[0]: end of directory
DIRENT_DELETED   = $E5                  ; name[0]: deleted entry

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

; ================================================================
; BLOCK DEVICE PRIMITIVES
; ================================================================
; Transfer whole 512-byte sectors between a host disk.img sector and a RAM
; buffer, driving the $FE24-$FE28 registers. The buffer pointer is passed in
; BLK_BUF_PTR ($3A/$3B) and is preserved across the call. The sector buffer
; spans two pages, so the transfer walks Y from 0..255 twice, bumping the
; pointer's high byte between the halves.
;
; In:  A = LBA low, X = LBA high; BLK_BUF_PTR -> 512-byte RAM buffer
; Out: carry clear = OK, carry set = device error (A = BLK_STATUS on error)

; ----------------------------------------------------------------
; _BLK_READ_SECTOR - read sector (A/X) into the RAM buffer
; ----------------------------------------------------------------
_BLK_READ_SECTOR:
    STA BLK_LBA                         ; LBA low (also resets the data-port index)
    STX BLK_LBA+1                       ; LBA high
    LDA #BLK_CMD_READ
    STA BLK_CMD                         ; device reads the sector into its buffer
    LDA BLK_STATUS
    BNE @err                            ; non-zero = error
    LDY #$00
@page0:
    LDA BLK_DATA                        ; first 256 bytes
    STA (BLK_BUF_PTR),Y
    INY
    BNE @page0
    INC BLK_BUF_PTR+1                   ; advance to the second page
@page1:
    LDA BLK_DATA                        ; second 256 bytes
    STA (BLK_BUF_PTR),Y
    INY
    BNE @page1
    DEC BLK_BUF_PTR+1                   ; restore the caller's pointer
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _BLK_WRITE_SECTOR - write the RAM buffer to sector (A/X)
; ----------------------------------------------------------------
_BLK_WRITE_SECTOR:
    STA BLK_LBA                         ; LBA low (resets the data-port index to 0)
    STX BLK_LBA+1                       ; LBA high
    LDY #$00
@page0:
    LDA (BLK_BUF_PTR),Y                 ; first 256 bytes -> data port
    STA BLK_DATA
    INY
    BNE @page0
    INC BLK_BUF_PTR+1                   ; advance to the second page
@page1:
    LDA (BLK_BUF_PTR),Y                 ; second 256 bytes -> data port
    STA BLK_DATA
    INY
    BNE @page1
    DEC BLK_BUF_PTR+1                   ; restore the caller's pointer
    LDA #BLK_CMD_WRITE
    STA BLK_CMD                         ; flush the buffer to the sector
    LDA BLK_STATUS
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

; ================================================================
; INTERNAL BLOCK/SECTOR HELPERS
; ================================================================

; ----------------------------------------------------------------
; _DOS_READ_SECTOR - read sector A/X into the block device buffer
; ----------------------------------------------------------------
; Unlike BLK_READ_SECTOR this does not copy to RAM; it just triggers the read so
; the caller can stream bytes from BLK_DATA. In: A=LBA lo, X=LBA hi. Out: carry
; set on device error.
_DOS_READ_SECTOR:
    STA BLK_LBA
    STX BLK_LBA+1
    LDA #BLK_CMD_READ
    STA BLK_CMD
    LDA BLK_STATUS
    BEQ @ok
    SEC
    RTS
@ok:
    CLC
    RTS

; ----------------------------------------------------------------
; _DOS_SKIP_BYTES - discard DOS_TMP bytes from the block data port
; ----------------------------------------------------------------
; Advances the block device's data-port index by reading and discarding. Used to
; seek to a field/entry within the buffered sector. Consumes DOS_TMP (16-bit).
_DOS_SKIP_BYTES:
@loop:
    LDA DOS_TMP
    ORA DOS_TMP+1
    BEQ @done
    LDA BLK_DATA                        ; consume one byte
    LDA DOS_TMP
    BNE @declo
    DEC DOS_TMP+1
@declo:
    DEC DOS_TMP
    BRA @loop
@done:
    RTS

; ================================================================
; FILESYSTEM: MOUNT
; ================================================================

; ----------------------------------------------------------------
; _FS_ENSURE_MOUNT - mount the volume if not already mounted
; ----------------------------------------------------------------
_FS_ENSURE_MOUNT:
    LDA DOS_MOUNTED
    BEQ _FS_MOUNT                       ; not mounted -> mount (tail call)
    CLC
    RTS

; ----------------------------------------------------------------
; _FS_MOUNT - read the boot sector, parse the BPB, cache geometry
; ----------------------------------------------------------------
; Computes and stores: sectors/cluster, FAT start, root-dir start, data start,
; root entry count. Out: carry set on error (DOS_MOUNTED left 0).
_FS_MOUNT:
    LDA #$00                            ; boot sector = LBA 0
    LDX #$00
    JSR _DOS_READ_SECTOR
    BCC @read_ok
    STZ DOS_MOUNTED                     ; read failed -> not mounted
    SEC
    RTS
@read_ok:
    ; seek to BPB offset $0B (BytesPerSector)
    LDA #$0B
    STA DOS_TMP
    STZ DOS_TMP+1
    JSR _DOS_SKIP_BYTES
    LDA BLK_DATA                        ; $0B BytesPerSector lo (assume 512)
    LDA BLK_DATA                        ; $0C BytesPerSector hi
    LDA BLK_DATA                        ; $0D SectorsPerCluster
    STA DOS_SEC_PER_CLUS
    LDA BLK_DATA                        ; $0E ReservedSectorCount lo = FAT start
    STA DOS_FAT_START
    LDA BLK_DATA                        ; $0F ReservedSectorCount hi
    STA DOS_FAT_START+1
    LDA BLK_DATA                        ; $10 NumberOfFATs
    STA DOS_NUMFATS
    LDA BLK_DATA                        ; $11 RootEntryCount lo
    STA DOS_ROOT_ENTS
    LDA BLK_DATA                        ; $12 RootEntryCount hi
    STA DOS_ROOT_ENTS+1
    LDA BLK_DATA                        ; $13 TotalSectors16 lo (unused)
    LDA BLK_DATA                        ; $14 TotalSectors16 hi (unused)
    LDA BLK_DATA                        ; $15 MediaDescriptor (unused)
    LDA BLK_DATA                        ; $16 FATSize16 lo
    STA DOS_FATSIZE
    LDA BLK_DATA                        ; $17 FATSize16 hi
    STA DOS_FATSIZE+1
    ; root_start = FAT_start + NumFATs * FATSize
    STZ DOS_TMP
    STZ DOS_TMP+1
    LDX DOS_NUMFATS
    BEQ @mul_done
@mul:
    CLC
    LDA DOS_TMP
    ADC DOS_FATSIZE
    STA DOS_TMP
    LDA DOS_TMP+1
    ADC DOS_FATSIZE+1
    STA DOS_TMP+1
    DEX
    BNE @mul
@mul_done:
    CLC
    LDA DOS_FAT_START
    ADC DOS_TMP
    STA DOS_ROOT_START
    LDA DOS_FAT_START+1
    ADC DOS_TMP+1
    STA DOS_ROOT_START+1
    ; root_sectors = (RootEntryCount + 15) >> 4   (32 bytes/entry, 16 entries/sector)
    CLC
    LDA DOS_ROOT_ENTS
    ADC #15
    STA DOS_TMP
    LDA DOS_ROOT_ENTS+1
    ADC #0
    STA DOS_TMP+1
    LDX #4
@sh:
    LSR DOS_TMP+1
    ROR DOS_TMP
    DEX
    BNE @sh
    ; data_start = root_start + root_sectors
    CLC
    LDA DOS_ROOT_START
    ADC DOS_TMP
    STA DOS_DATA_START
    LDA DOS_ROOT_START+1
    ADC DOS_TMP+1
    STA DOS_DATA_START+1
    LDA #1
    STA DOS_MOUNTED
    CLC
    RTS

; ================================================================
; FILESYSTEM: DIRECTORY ENUMERATION
; ================================================================

; ----------------------------------------------------------------
; _FS_DIR_FIRST - begin a root-directory scan; return the first entry
; ----------------------------------------------------------------
; Out: carry clear and DOS_ENTRY filled with the first valid 8.3 entry, or carry
; set if the directory is empty / unreadable.
_FS_DIR_FIRST:
    JSR _FS_ENSURE_MOUNT
    BCS @err
    LDA DOS_ROOT_START
    STA DOS_DIR_LBA
    LDA DOS_ROOT_START+1
    STA DOS_DIR_LBA+1
    STZ DOS_DIR_IDX
    LDA DOS_ROOT_ENTS
    STA DOS_DIR_LEFT
    LDA DOS_ROOT_ENTS+1
    STA DOS_DIR_LEFT+1
    BRA _FS_DIR_NEXT
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _FS_DIR_NEXT - return the next valid root-directory entry
; ----------------------------------------------------------------
; Skips deleted, long-file-name, and volume-label entries; stops at the
; end-of-directory marker. Out: carry clear and DOS_ENTRY filled, or carry set
; when no more entries.
_FS_DIR_NEXT:
@loop:
    LDA DOS_DIR_LEFT                    ; entries remaining?
    ORA DOS_DIR_LEFT+1
    BNE @have
    SEC
    RTS
@have:
    JSR _DOS_READ_DIR_ENTRY             ; DOS_DIR_LBA/IDX -> DOS_ENTRY
    BCS @stop                           ; read error -> end
    JSR _DOS_DIR_ADVANCE                ; bump cursor, decrement DOS_DIR_LEFT
    LDA DOS_ENTRY+DIR_NAME
    BNE @notend
    ; end-of-directory marker: no more entries
    STZ DOS_DIR_LEFT
    STZ DOS_DIR_LEFT+1
    SEC
    RTS
@notend:
    CMP #DIRENT_DELETED
    BEQ @loop                           ; deleted -> skip
    LDA DOS_ENTRY+DIR_ATTR
    AND #$0F
    CMP #ATTR_LFN
    BEQ @loop                           ; long-file-name fragment -> skip
    LDA DOS_ENTRY+DIR_ATTR
    AND #ATTR_VOLUME
    BNE @loop                           ; volume label -> skip
    CLC                                 ; valid entry
    RTS
@stop:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_READ_DIR_ENTRY - load the entry at DOS_DIR_LBA/DOS_DIR_IDX
; ----------------------------------------------------------------
; Re-reads the directory sector and skips to the indexed 32-byte entry, so each
; call is self-contained. Fills DOS_ENTRY (32 bytes). Out: carry set on error.
_DOS_READ_DIR_ENTRY:
    LDA DOS_DIR_LBA
    LDX DOS_DIR_LBA+1
    JSR _DOS_READ_SECTOR
    BCS @err
    ; skip DOS_DIR_IDX * 32 bytes
    LDA DOS_DIR_IDX
    STA DOS_TMP
    STZ DOS_TMP+1
    LDX #5                              ; * 32 = << 5
@sh:
    ASL DOS_TMP
    ROL DOS_TMP+1
    DEX
    BNE @sh
    JSR _DOS_SKIP_BYTES
    LDY #$00
@rd:
    LDA BLK_DATA
    STA DOS_ENTRY,Y
    INY
    CPY #32
    BNE @rd
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_DIR_ADVANCE - advance the directory cursor by one entry
; ----------------------------------------------------------------
_DOS_DIR_ADVANCE:
    INC DOS_DIR_IDX
    LDA DOS_DIR_IDX
    CMP #16                             ; 16 entries per 512-byte sector
    BCC @dec
    STZ DOS_DIR_IDX
    INC DOS_DIR_LBA
    BNE @dec
    INC DOS_DIR_LBA+1
@dec:
    LDA DOS_DIR_LEFT                    ; DOS_DIR_LEFT--
    BNE @declo
    DEC DOS_DIR_LEFT+1
@declo:
    DEC DOS_DIR_LEFT
    RTS

; ================================================================
; FILESYSTEM: FILE READ (FS_OPEN / FS_GETB / FS_CLOSE)
; ================================================================

; ----------------------------------------------------------------
; _FS_OPEN - open a file by name for reading
; ----------------------------------------------------------------
; In:  A/X = pointer to a null-terminated filename ("NAME.EXT", case-insensitive).
;      (Y = mode is reserved for write; read-only for now.)
; Out: carry clear and the open-file cursor armed (first data sector loaded), or
;      carry set if not mounted / not found. Only one file open at a time.
;
; NOTE: FS_GETB streams bytes straight from the block device's sector buffer, so
; the caller must not issue other block-device operations between FS_OPEN and
; FS_CLOSE (the "one open file at a time" contract).
_FS_OPEN:
    STA DOS_PTR
    STX DOS_PTR+1
    JSR _DOS_PARSE_NAME83               ; -> DOS_NAME83 (11-byte 8.3)
    JSR _FS_DIR_FIRST                   ; mounts; first entry in DOS_ENTRY
    BCS @err
@check:
    LDY #$00
@cmp:
    LDA DOS_ENTRY,Y
    CMP DOS_NAME83,Y
    BNE @nextent
    INY
    CPY #11
    BNE @cmp
    BRA @found                          ; all 11 bytes matched
@nextent:
    JSR _FS_DIR_NEXT
    BCC @check
@err:
    SEC                                 ; not found / not mounted
    RTS
@found:
    LDA DOS_ENTRY+DIR_CLUSTER_LO
    STA DOS_F_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_F_CLUS+1
    LDA DOS_ENTRY+DIR_SIZE
    STA DOS_F_LEFT
    LDA DOS_ENTRY+DIR_SIZE+1
    STA DOS_F_LEFT+1
    LDA DOS_ENTRY+DIR_SIZE+2
    STA DOS_F_LEFT+2
    LDA DOS_ENTRY+DIR_SIZE+3
    STA DOS_F_LEFT+3
    STZ DOS_F_SIC
    STZ DOS_F_OFF
    STZ DOS_F_OFF+1
    ; empty file: opened, but nothing to load (FS_GETB returns EOF immediately)
    LDA DOS_F_LEFT
    ORA DOS_F_LEFT+1
    ORA DOS_F_LEFT+2
    ORA DOS_F_LEFT+3
    BEQ @ok
    ; load the first data sector
    JSR _DOS_CLUS_TO_LBA
    LDA DOS_F_LBA
    LDX DOS_F_LBA+1
    JSR _DOS_READ_SECTOR
    BCS @err
@ok:
    CLC
    RTS

; ----------------------------------------------------------------
; _FS_GETB - read the next byte of the open file
; ----------------------------------------------------------------
; Out: carry clear and A = byte, or carry set = EOF. Streams from the block
; device buffer; crosses sector and cluster boundaries transparently.
_FS_GETB:
    LDA DOS_F_LEFT
    ORA DOS_F_LEFT+1
    ORA DOS_F_LEFT+2
    ORA DOS_F_LEFT+3
    BNE @have
    SEC                                 ; EOF
    RTS
@have:
    ; need the next sector? (offset reached 512 = $0200)
    LDA DOS_F_OFF
    BNE @read
    LDA DOS_F_OFF+1
    CMP #$02
    BNE @read
    JSR _DOS_NEXT_SECTOR
    BCS @eof                            ; chain ended unexpectedly
@read:
    LDA BLK_DATA
    PHA
    INC DOS_F_OFF                       ; offset++
    BNE @noff
    INC DOS_F_OFF+1
@noff:
    JSR _DOS_DEC_LEFT                   ; bytes-remaining--
    PLA
    CLC
    RTS
@eof:
    SEC
    RTS

; ----------------------------------------------------------------
; _FS_CLOSE - close the open file (read: just clears the cursor)
; ----------------------------------------------------------------
_FS_CLOSE:
    STZ DOS_F_LEFT
    STZ DOS_F_LEFT+1
    STZ DOS_F_LEFT+2
    STZ DOS_F_LEFT+3
    CLC
    RTS

; ----------------------------------------------------------------
; _FS_PUTB - STUB (file write lands in phase 3)
; ----------------------------------------------------------------
_FS_PUTB:
    SEC
    RTS

; ================================================================
; FILESYSTEM: INTERNAL READ HELPERS
; ================================================================

; ----------------------------------------------------------------
; _DOS_PARSE_NAME83 - (DOS_PTR) "NAME.EXT" -> DOS_NAME83 (11, padded)
; ----------------------------------------------------------------
_DOS_PARSE_NAME83:
    LDX #$00                            ; space-fill the 11-byte buffer
@fill:
    LDA #' '
    STA DOS_NAME83,X
    INX
    CPX #11
    BNE @fill
    LDY #$00                            ; source index
    LDX #$00                            ; dest index (base = 0..7)
@base:
    LDA (DOS_PTR),Y
    BEQ @done
    CMP #'.'
    BEQ @dot
    JSR _DOS_UPCASE
    CPX #8
    BCS @nextb                          ; base full -> ignore extra chars
    STA DOS_NAME83,X
    INX
@nextb:
    INY
    BRA @base
@dot:
    INY                                 ; skip the '.'
    LDX #8                              ; dest = extension (8..10)
@ext:
    LDA (DOS_PTR),Y
    BEQ @done
    JSR _DOS_UPCASE
    CPX #11
    BCS @nexte
    STA DOS_NAME83,X
    INX
@nexte:
    INY
    BRA @ext
@done:
    RTS

; ----------------------------------------------------------------
; _DOS_UPCASE - fold a lowercase ASCII letter in A to uppercase
; ----------------------------------------------------------------
_DOS_UPCASE:
    CMP #'a'
    BCC @ok
    CMP #'z'+1
    BCS @ok
    AND #$DF
@ok:
    RTS

; ----------------------------------------------------------------
; _DOS_CLUS_TO_LBA - DOS_F_CLUS -> DOS_F_LBA (first sector of cluster)
; ----------------------------------------------------------------
; LBA = DataStart + (cluster - 2) * SectorsPerCluster.
_DOS_CLUS_TO_LBA:
    SEC
    LDA DOS_F_CLUS
    SBC #2
    STA DOS_TMP
    LDA DOS_F_CLUS+1
    SBC #0
    STA DOS_TMP+1
    STZ DOS_TMP2
    STZ DOS_TMP2+1
    LDX DOS_SEC_PER_CLUS
    BEQ @add
@mul:
    CLC
    LDA DOS_TMP2
    ADC DOS_TMP
    STA DOS_TMP2
    LDA DOS_TMP2+1
    ADC DOS_TMP+1
    STA DOS_TMP2+1
    DEX
    BNE @mul
@add:
    CLC
    LDA DOS_DATA_START
    ADC DOS_TMP2
    STA DOS_F_LBA
    LDA DOS_DATA_START+1
    ADC DOS_TMP2+1
    STA DOS_F_LBA+1
    RTS

; ----------------------------------------------------------------
; _DOS_NEXT_CLUSTER - follow the FAT chain: DOS_F_CLUS -> next cluster
; ----------------------------------------------------------------
; Out: carry clear and DOS_F_CLUS = next cluster, or carry set at end-of-chain /
; on a read error. FAT byte offset = cluster*2 (17-bit); split into a sector
; index (offset>>9) and an in-sector offset (offset & $1FF).
_DOS_NEXT_CLUSTER:
    LDA DOS_F_CLUS
    ASL                                 ; lo<<1, C=c0
    STA DOS_TMP                         ; offset low byte (= lo2)
    LDA DOS_F_CLUS+1
    ROL                                 ; hi<<1 | c0, C=c1 (bit16)
    STA DOS_TMP+1                       ; mid byte (mid2)
    LDA #$00
    ROL                                 ; A = c1
    STA DOS_TMP2                        ; c1
    LDA DOS_TMP+1                        ; offset bit8 = mid2 & 1
    AND #$01
    STA DOS_TMP2+1                      ; offset high byte (0/1)
    ; sector index = ((c1<<8) | mid2) >> 1   (sector hi is 0)
    LDA DOS_TMP2                        ; c1
    LSR
    LDA DOS_TMP+1                       ; mid2
    ROR
    STA DOS_TMP2                        ; sector index (low; high = 0)
    ; FAT sector = FAT_start + sector index
    CLC
    LDA DOS_FAT_START
    ADC DOS_TMP2
    PHA
    LDA DOS_FAT_START+1
    ADC #$00
    TAX
    PLA
    JSR _DOS_READ_SECTOR
    BCS @err
    ; skip to the entry: offset = DOS_TMP (lo) / DOS_TMP2+1 (hi)
    LDA DOS_TMP2+1
    STA DOS_TMP+1
    JSR _DOS_SKIP_BYTES                 ; consumes DOS_TMP (offset)
    LDA BLK_DATA                        ; next cluster low
    STA DOS_F_CLUS
    LDA BLK_DATA                        ; next cluster high
    STA DOS_F_CLUS+1
    ; end of chain? (>= $FFF8)
    LDA DOS_F_CLUS+1
    CMP #$FF
    BNE @ok
    LDA DOS_F_CLUS
    CMP #$F8
    BCS @err                            ; $FFF8..$FFFF -> EOC
@ok:
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_NEXT_SECTOR - advance the open file to its next data sector
; ----------------------------------------------------------------
; Steps within the current cluster, or follows the FAT chain at a cluster
; boundary. Loads the new sector and resets the offset. Out: carry set at EOC /
; on error.
_DOS_NEXT_SECTOR:
    INC DOS_F_SIC
    LDA DOS_F_SIC
    CMP DOS_SEC_PER_CLUS
    BCC @same
    JSR _DOS_NEXT_CLUSTER
    BCS @err
    STZ DOS_F_SIC
    JSR _DOS_CLUS_TO_LBA
    BRA @load
@same:
    INC DOS_F_LBA
    BNE @load
    INC DOS_F_LBA+1
@load:
    STZ DOS_F_OFF
    STZ DOS_F_OFF+1
    LDA DOS_F_LBA
    LDX DOS_F_LBA+1
    JSR _DOS_READ_SECTOR
    RTS                                 ; carry reflects the read
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_DEC_LEFT - decrement the 32-bit bytes-remaining counter
; ----------------------------------------------------------------
_DOS_DEC_LEFT:
    LDA DOS_F_LEFT
    SEC
    SBC #1
    STA DOS_F_LEFT
    LDA DOS_F_LEFT+1
    SBC #0
    STA DOS_F_LEFT+1
    LDA DOS_F_LEFT+2
    SBC #0
    STA DOS_F_LEFT+2
    LDA DOS_F_LEFT+3
    SBC #0
    STA DOS_F_LEFT+3
    RTS

; ----------------------------------------------------------------
; _DOS_COLD - DOS shell cold entry (placeholder; wired in phase 4)
; ----------------------------------------------------------------
_DOS_COLD:
    RTS

; ================================================================
; DOS ABI JUMP TABLE ($AF00) - the stable entry points
; ================================================================
; Callers (the kernel BIOS, the monitor, user programs) bind to these fixed
; addresses. New entries are appended at the end so existing addresses never
; move.
.segment "DOSJUMP"
.org $AF00

DOS_COLD:         JMP _DOS_COLD          ; $AF00 - DOS shell cold entry (phase 4)
FS_OPEN:          JMP _FS_OPEN           ; $AF03
FS_GETB:          JMP _FS_GETB           ; $AF06
FS_PUTB:          JMP _FS_PUTB           ; $AF09
FS_CLOSE:         JMP _FS_CLOSE          ; $AF0C
FS_DIR_FIRST:     JMP _FS_DIR_FIRST      ; $AF0F
FS_DIR_NEXT:      JMP _FS_DIR_NEXT       ; $AF12
BLK_READ_SECTOR:  JMP _BLK_READ_SECTOR   ; $AF15
BLK_WRITE_SECTOR: JMP _BLK_WRITE_SECTOR  ; $AF18
