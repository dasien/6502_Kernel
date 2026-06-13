/**
 * @file fat16_image.h
 * @brief Minimal FAT16 disk-image builder for MFC-DOS filesystem tests.
 *
 * Builds an in-memory FAT16 volume (boot sector/BPB, one FAT, a single-sector-
 * region root directory, and a contiguous data region) from a list of 8.3 files.
 * The layout it produces is what the 6502 FAT16 driver in src/kernel/dos/dos.asm
 * is validated against; the same builder is intended to graduate into the
 * standalone disk-image generator tool in step 2.4.
 *
 * Deliberately simple (matches the driver's starting scope): 512-byte sectors,
 * 1 sector/cluster, 1 FAT, root directory only, 8.3 names, files allocated to
 * contiguous clusters. Not a general FAT formatter.
 */

#ifndef MFCDOS_TEST_FAT16_IMAGE_H
#define MFCDOS_TEST_FAT16_IMAGE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mfcdos_test {

struct Fat16File {
    std::string name;          ///< 8.3 name, e.g. "HELLO.TXT" (case-insensitive)
    std::vector<uint8_t> data; ///< file contents
};

class Fat16ImageBuilder {
public:
    static constexpr uint32_t kBytesPerSector = 512;
    static constexpr uint8_t kSectorsPerCluster = 1;
    static constexpr uint16_t kReservedSectors = 1;
    static constexpr uint8_t kNumFats = 1;
    static constexpr uint16_t kRootEntries = 16;   // one 512-byte root sector
    static constexpr uint16_t kDataClusters = 128; // small but genuine FAT16

    // Build the image bytes for the given files. Files are placed in directory
    // order, each in contiguous clusters starting at cluster 2.
    static std::vector<uint8_t> build(const std::vector<Fat16File> &files) {
        const uint16_t fatSize = fatSectors();
        const uint16_t rootSectors = kRootEntries * 32 / kBytesPerSector; // = 1
        const uint16_t fatStart = kReservedSectors;
        const uint16_t rootStart = fatStart + kNumFats * fatSize;
        const uint16_t dataStart = rootStart + rootSectors;
        const uint32_t totalSectors = dataStart + kDataClusters;

        std::vector<uint8_t> img(totalSectors * kBytesPerSector, 0x00);

        writeBootSector(img, fatSize, totalSectors);

        // FAT[0]/FAT[1] are reserved (media + EOC markers).
        std::vector<uint16_t> fat(2 + kDataClusters, 0x0000);
        fat[0] = 0xFFF8;
        fat[1] = 0xFFFF;

        // Root directory entries are written into the root sector.
        uint8_t *root = &img[rootStart * kBytesPerSector];

        uint16_t nextCluster = 2;
        for (size_t f = 0; f < files.size(); ++f) {
            const Fat16File &file = files[f];
            const uint32_t size = static_cast<uint32_t>(file.data.size());
            const uint16_t clusters =
                size == 0 ? 0
                          : static_cast<uint16_t>((size + clusterBytes() - 1) / clusterBytes());
            const uint16_t firstCluster = clusters ? nextCluster : 0;

            // Chain the clusters and copy the data.
            for (uint16_t c = 0; c < clusters; ++c) {
                const uint16_t cluster = nextCluster + c;
                fat[cluster] = (c + 1 < clusters) ? (cluster + 1) : 0xFFFF; // EOC
                const uint32_t lba = dataStart + (cluster - 2) * kSectorsPerCluster;
                const uint32_t off = c * clusterBytes();
                const uint32_t n = std::min<uint32_t>(clusterBytes(), size - off);
                std::memcpy(&img[lba * kBytesPerSector], &file.data[off], n);
            }
            nextCluster += clusters;

            writeDirEntry(root + f * 32, file.name, firstCluster, size);
        }

        // Serialize the FAT (little-endian 16-bit entries).
        uint8_t *fatBytes = &img[fatStart * kBytesPerSector];
        for (size_t i = 0; i < fat.size(); ++i) {
            fatBytes[i * 2] = fat[i] & 0xFF;
            fatBytes[i * 2 + 1] = (fat[i] >> 8) & 0xFF;
        }

        return img;
    }

private:
    static constexpr uint32_t clusterBytes() {
        return kBytesPerSector * kSectorsPerCluster;
    }

    static uint16_t fatSectors() {
        const uint32_t entries = 2 + kDataClusters;
        const uint32_t bytes = entries * 2;
        return static_cast<uint16_t>((bytes + kBytesPerSector - 1) / kBytesPerSector);
    }

    static void put16(uint8_t *p, uint16_t v) {
        p[0] = v & 0xFF;
        p[1] = (v >> 8) & 0xFF;
    }
    static void put32(uint8_t *p, uint32_t v) {
        p[0] = v & 0xFF;
        p[1] = (v >> 8) & 0xFF;
        p[2] = (v >> 16) & 0xFF;
        p[3] = (v >> 24) & 0xFF;
    }

    static void writeBootSector(std::vector<uint8_t> &img, uint16_t fatSize,
                                uint32_t totalSectors) {
        uint8_t *b = img.data();
        b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;          // jump (cosmetic)
        std::memcpy(&b[3], "MFCDOS  ", 8);              // OEM name
        put16(&b[0x0B], kBytesPerSector);
        b[0x0D] = kSectorsPerCluster;
        put16(&b[0x0E], kReservedSectors);
        b[0x10] = kNumFats;
        put16(&b[0x11], kRootEntries);
        if (totalSectors < 0x10000) {
            put16(&b[0x13], static_cast<uint16_t>(totalSectors)); // TotalSectors16
        } else {
            put16(&b[0x13], 0);
            put32(&b[0x20], totalSectors);                        // TotalSectors32
        }
        b[0x15] = 0xF8;                                  // media descriptor
        put16(&b[0x16], fatSize);                        // FATSize16
        put16(&b[0x18], 63);                             // sectors/track (cosmetic)
        put16(&b[0x1A], 255);                            // heads (cosmetic)
        b[0x26] = 0x29;                                  // extended boot signature
        std::memcpy(&b[0x2B], "MFCDOS VOL ", 11);        // volume label
        std::memcpy(&b[0x36], "FAT16   ", 8);            // fs type
        b[0x1FE] = 0x55; b[0x1FF] = 0xAA;                // boot signature
    }

    // Write a directory entry: 8.3 name (space-padded, uppercased), archive
    // attribute, first cluster, and size.
    static void writeDirEntry(uint8_t *e, const std::string &name,
                              uint16_t firstCluster, uint32_t size) {
        std::memset(e, ' ', 11);
        size_t dot = name.find('.');
        std::string base = (dot == std::string::npos) ? name : name.substr(0, dot);
        std::string ext = (dot == std::string::npos) ? "" : name.substr(dot + 1);
        for (size_t i = 0; i < base.size() && i < 8; ++i)
            e[i] = static_cast<uint8_t>(toupper(base[i]));
        for (size_t i = 0; i < ext.size() && i < 3; ++i)
            e[8 + i] = static_cast<uint8_t>(toupper(ext[i]));
        e[0x0B] = 0x20; // archive attribute
        put16(&e[0x1A], firstCluster);
        put32(&e[0x1C], size);
    }
};

} // namespace mfcdos_test

#endif // MFCDOS_TEST_FAT16_IMAGE_H
