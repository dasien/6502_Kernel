# The disk image

How `disk.img` is assembled on the host, what decides its contents, and the `mkdisk`
tool that builds it. Driving the disk from inside the machine, with `CATALOG`, `TYPE`,
drawers and `IMPORT` and `EXPORT`, is covered in [DOS.md](DOS.md).

## The catalog is the source of truth

The GUI loads `cmake-build-debug/disk.img`, which is assembled from
`programs/catalog.txt`. That file is the single source of truth for everything that
can go on a disk. It holds one section per item, saying where the item's files live,
how to build them or that they are committed content needing no build, and where each
one lands on the disk.

Files are declared by what they are. A `program` is the `.PRG` itself, `data` is what
the program reads and writes at run time, and a `doc` is something for a human to
`TYPE`. The distinction exists so that `data` can never be separated from the program
that needs it. Naming `term` inescapably brings `SYSTEM/DIAL.LST` with it, because the
catalog records that TERM reads and writes that file.

An entry also carries the build recipe, meaning its `sources`, its `config` and an
optional `include`, so CMake compiles and links the program itself. There are no
per-program build scripts. CMake refuses to configure if a directory under
`programs/` has an `ld65` config but no catalog entry, which catches exactly the
failure the catalog exists to prevent, where a program builds perfectly well and
simply is not on the disk.

The build derives both the staging commands and the `diskmap.txt` that `mkdisk`
consumes, so adding a program is one catalog entry rather than three separate edits
that fail silently if you miss one.

Drawers grow across as many FAT16 clusters as they need, so a drawer is not
capped at one cluster of files.

## Building it

```bash
ninja disk          # build the programs, then assemble the image
ninja everything    # ...and the ROMs and the app as well
```

Both are explicit, and a plain `ninja` never rewrites the disk. The `disk` target
builds every catalog program before staging it, so the image can never carry a stale
binary.

### The build will refuse while a machine is using the image

`ninja disk` rewrites `disk.img` in place, and the block device re-opens that file by
path on every sector access. A rebuild while the emulator is running would therefore be
adopted mid-session, with DOS still holding cluster state allocated against the old
FAT. A save in flight would then stamp a directory entry pointing at a chain the new
image does not agree with.

A running machine therefore takes a shared advisory claim on its image, and `mkdisk`
takes an exclusive one before it writes.

```
mkdisk: .../disk.img is in use by a running machine, so it was NOT rewritten.
        Quit the emulator and run this again, or pass --force to overwrite it
        anyway (a machine still holding it can then corrupt the new image).
```

Quit the emulator and run the command again, or pass `--force` if you know the machine
is not writing. The claim is advisory and dies with the process, so there is never a
stale lock to clear.

## `mkdisk` standalone

The host tool (`cmake-build-debug/bin/mkdisk`) also works on its own:

```bash
mkdisk create <image> <diskmap.txt>   # build a fresh image from a bundle
mkdisk read   <image> <outdir>        # extract an image into a bundle (+ diskmap.txt)
mkdisk update <image> <diskmap.txt>   # replace/add listed files, keep the rest
```

A bundle is a directory holding a `diskmap.txt` alongside the files themselves, laid
out to mirror the disk. The map has one disk path per line, and a leading `DRAWER/`
names a one-level drawer. `create` and `update` read each path's bytes from
`<diskmap-dir>/<path>`, and the tool does no searching, because the bundle already
holds everything it needs. `read` is the inverse and round-trips cleanly, so a `read`
followed by a `create` reproduces an equivalent image.

`read` never claims the image, since it only reads.

## Geometry

Images are always built at 4096 data clusters, which is a fixed 2,122,752 bytes
whatever the catalog happens to hold. They use 512-byte sectors, one sector per
cluster, and a 512-entry root directory.

The cluster count is not arbitrary, because FAT type is derived from it rather than
declared. Below 4085 clusters the specification says FAT12, so a host tool will parse
a 16-bit table as 12-bit and report corruption on a perfectly good image. The
`"FAT16   "` string in the boot sector is informational and must not be used to
determine the type. 4096 is the smallest count that is unambiguously FAT16, and that
is the only reason `fsck.fat` can check these images at all.

Because the geometry is fixed at format time, neither catalog growth nor anything the
6502 writes can change it. The guest allocates within a BPB it cannot move.
