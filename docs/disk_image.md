# The disk image

How `disk.img` is assembled on the host, what decides its contents, and the
`mkdisk` tool that builds it. For driving the disk *from inside* the machine —
`CATALOG`, `TYPE`, drawers, `IMPORT`/`EXPORT` — see [DOS.md](DOS.md).

## The catalog is the source of truth

The GUI loads `cmake-build-debug/disk.img`, assembled from
**`programs/catalog.txt`** — the single source of truth for everything that can
go on a disk. One section per item, saying where its files live, how to build
them (or that they are committed content needing no build), and where each lands
on the disk.

Files are declared by what they *are*: a `program` (the `.PRG`), `data` the
program reads and writes at run time, or a `doc` for a human to `TYPE`. The
distinction is there so `data` can never be separated from the program that needs
it — naming `term` inescapably brings `SYSTEM/DIAL.LST`, because the catalog
records that TERM reads and writes it.

An entry also carries the **build recipe** — its `sources`, its `config`, and an
optional `include` — so CMake compiles and links the program itself. There are no
per-program build scripts. CMake refuses to configure if a `programs/*/`
directory has an `ld65` config but no catalog entry, which is the failure the
catalog exists to prevent: the program builds fine and simply is not on the disk.

The build derives both the staging commands and the `diskmap.txt` that `mkdisk`
consumes, so adding a program is one catalog entry rather than three edits that
fail silently if you miss one.

Drawers grow across as many FAT16 clusters as they need, so a drawer is not
capped at one cluster of files.

## Building it

```bash
ninja disk          # build the programs, then assemble the image
ninja everything    # ...and the ROMs and the app as well
```

Both are explicit — a plain `ninja` never rewrites the disk. `disk` builds every
catalog program before staging it, so the image can never carry a stale binary.

### The build will refuse while a machine is using the image

`ninja disk` rewrites `disk.img` in place, and the block device re-opens it by
path on every sector access — so a rebuild while the emulator is running would be
adopted mid-session, with the DOS still holding cluster state allocated against
the old FAT. A save in flight would then stamp a directory entry pointing at a
chain the new image does not agree with.

So a running machine takes a shared advisory claim on its image, and `mkdisk`
takes an exclusive one before writing:

```
mkdisk: .../disk.img is in use by a running machine, so it was NOT rewritten.
        Quit the emulator and run this again, or pass --force to overwrite it
        anyway (a machine still holding it can then corrupt the new image).
```

Quit the emulator and run it again, or pass `--force` if you know the machine
isn't writing. The claim is advisory and dies with the process, so there is no
stale lock to clear.

## `mkdisk` standalone

The host tool (`cmake-build-debug/bin/mkdisk`) also works on its own:

```bash
mkdisk create <image> <diskmap.txt>   # build a fresh image from a bundle
mkdisk read   <image> <outdir>        # extract an image into a bundle (+ diskmap.txt)
mkdisk update <image> <diskmap.txt>   # replace/add listed files, keep the rest
```

A **bundle** is a directory holding a `diskmap.txt` — one disk path per line,
where a leading `DRAWER/` names a one-level drawer — plus the files laid out
mirroring the disk. `create` and `update` read each path's bytes from
`<diskmap-dir>/<path>`; the tool does no searching, because the bundle already
holds the files. `read` is the inverse and round-trips: `read` then `create`
reproduces an equivalent image.

`read` never claims the image, since it only reads.

## Geometry

Images are always built at 4096 data clusters — a fixed **2,122,752 bytes**,
whatever the catalog holds — with 512-byte sectors, one sector per cluster, and a
512-entry root directory.

The cluster count is not arbitrary. **FAT type is derived from it, not
declared**: under 4085 clusters the specification says FAT12, so a host tool
parses a 16-bit table as 12-bit and reports corruption on a perfectly good image.
The `"FAT16   "` string in the boot sector is informational and must not be used
to determine type. 4096 is the smallest count that is unambiguously FAT16, which
is why `fsck.fat` can check our images at all.

Because the geometry is fixed at format time, neither catalog growth nor anything
the 6502 writes can change it — the guest allocates within a BPB it cannot move.
