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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace mfcdos_test {

struct Fat16File {
    std::string name;          ///< 8.3 name, e.g. "HELLO.TXT" (case-insensitive)
    std::vector<uint8_t> data; ///< file contents
    std::string drawer;        ///< "" = root; else a one-level drawer (subdir) name
};

class Fat16ImageBuilder {
public:
    static constexpr uint32_t kBytesPerSector = 512;
    static constexpr uint8_t kSectorsPerCluster = 1;
    static constexpr uint16_t kReservedSectors = 1;
    static constexpr uint8_t kNumFats = 1;
    static constexpr uint16_t kRootEntries = 16;   // one 512-byte root sector
    static constexpr uint32_t kDefaultDataClusters = 128; // small + fast for tests
    // A genuine FAT16 volume needs >= 4085 clusters, or host OSes treat it as
    // FAT12. The mkfat16 tool uses this so the image is host-mountable.
    static constexpr uint32_t kHostFat16Clusters = 4096;

    // Build the image bytes for the given files. Files are placed in directory
    // order, each in contiguous clusters starting at cluster 2. dataClusters
    // sets the volume size (default small for tests; kHostFat16Clusters for a
    // host-mountable image).
    static std::vector<uint8_t> build(const std::vector<Fat16File> &files,
                                      uint32_t dataClusters = kDefaultDataClusters) {
        const uint16_t fatSize = fatSectors(dataClusters);
        const uint16_t rootSectors = kRootEntries * 32 / kBytesPerSector; // = 1
        const uint16_t fatStart = kReservedSectors;
        const uint16_t rootStart = fatStart + kNumFats * fatSize;
        const uint16_t dataStart = rootStart + rootSectors;
        const uint32_t totalSectors = dataStart + dataClusters;

        std::vector<uint8_t> img(totalSectors * kBytesPerSector, 0x00);

        writeBootSector(img, fatSize, totalSectors);

        // FAT[0]/FAT[1] are reserved (media + EOC markers).
        std::vector<uint16_t> fat(2 + dataClusters, 0x0000);
        fat[0] = 0xFFF8;
        fat[1] = 0xFFFF;

        uint8_t *root = &img[rootStart * kBytesPerSector];
        uint16_t nextCluster = 2;

        // Allocate one contiguous cluster run for a file's data, chain the FAT,
        // and copy the bytes in. Returns the first cluster (0 for an empty file).
        auto placeData = [&](const Fat16File &file) -> uint16_t {
            const uint32_t size = static_cast<uint32_t>(file.data.size());
            // 32-bit: a file >= 32 MB overflows a uint16_t cluster count, and a
            // count of 0 would emit an entry with firstCluster 0 and a huge size.
            const uint32_t clusters =
                size == 0 ? 0 : (size + clusterBytes() - 1) / clusterBytes();
            // Capacity check, as the drawer-directory path below already does. Without
            // it, fat[cluster] and the memcpy into img both indexed past their vectors
            // -- a bundle larger than the volume corrupted the heap instead of failing.
            if (static_cast<uint32_t>(nextCluster) + clusters > 2u + dataClusters)
                throw std::runtime_error("fat16: image full (raise dataClusters) placing '" +
                                         file.name + "'");
            const uint16_t first = clusters ? nextCluster : 0;
            for (uint32_t c = 0; c < clusters; ++c) {
                const uint16_t cluster = static_cast<uint16_t>(nextCluster + c);
                fat[cluster] = (c + 1 < clusters) ? static_cast<uint16_t>(cluster + 1) : 0xFFFF;
                const uint32_t lba = dataStart + (cluster - 2) * kSectorsPerCluster;
                const uint32_t off = c * clusterBytes();
                const uint32_t n = std::min<uint32_t>(clusterBytes(), size - off);
                std::memcpy(&img[lba * kBytesPerSector], &file.data[off], n);
            }
            nextCluster = static_cast<uint16_t>(nextCluster + clusters);
            return first;
        };

        // Partition into root files and one-level drawers (first-seen order).
        std::vector<const Fat16File *> rootFiles;
        std::vector<std::string> drawerOrder;
        std::vector<std::vector<const Fat16File *>> drawerFiles;
        for (const Fat16File &f : files) {
            if (f.drawer.empty()) { rootFiles.push_back(&f); continue; }
            size_t d = 0;
            for (; d < drawerOrder.size(); ++d) if (drawerOrder[d] == f.drawer) break;
            if (d == drawerOrder.size()) { drawerOrder.push_back(f.drawer); drawerFiles.emplace_back(); }
            drawerFiles[d].push_back(&f);
        }
        // Root is a fixed region of at most kRootEntries entries (root files +
        // drawers); fail loudly rather than overflow it. A drawer's own directory
        // is NOT fixed -- it grows across clusters below.
        const size_t perCluster = clusterBytes() / 32;
        if (rootFiles.size() + drawerOrder.size() > kRootEntries)
            throw std::runtime_error("fat16: too many root entries (raise kRootEntries or use drawers)");

        size_t rootIdx = 0;
        for (const Fat16File *file : rootFiles) {
            const uint32_t size = static_cast<uint32_t>(file->data.size());
            writeDirEntry(root + rootIdx++ * 32, file->name, placeData(*file), size);
        }
        for (size_t d = 0; d < drawerOrder.size(); ++d) {
            // A drawer's directory grows across as many clusters as its entries
            // need ('.' + '..' + one per file). Allocate them contiguously and
            // chain them in the FAT, just like a file's data -- the 6502 DOS and
            // the reader both walk the directory's cluster chain.
            const size_t entries = drawerFiles[d].size() + 2;   // incl. '.' and '..'
            const uint16_t dirClusters =
                static_cast<uint16_t>((entries + perCluster - 1) / perCluster);
            const uint16_t firstDir = nextCluster;
            if (static_cast<uint32_t>(firstDir) + dirClusters > 2u + dataClusters)
                throw std::runtime_error("fat16: image full (raise dataClusters)");
            for (uint16_t c = 0; c < dirClusters; ++c)
                fat[firstDir + c] = (c + 1 < dirClusters) ? (firstDir + c + 1) : 0xFFFF;
            nextCluster += dirClusters;

            // Contiguous clusters are contiguous bytes in the image, so the entries
            // can be written linearly across the whole directory region.
            uint8_t *dir = &img[(dataStart + (firstDir - 2) * kSectorsPerCluster) * kBytesPerSector];
            writeDotEntries(dir, firstDir);
            size_t slot = 2;                              // entries after '.' and '..'
            for (const Fat16File *file : drawerFiles[d]) {
                const uint32_t size = static_cast<uint32_t>(file->data.size());
                writeDirEntry(dir + slot++ * 32, file->name, placeData(*file), size);
            }
            writeDirEntry(root + rootIdx++ * 32, drawerOrder[d], firstDir, 0, 0x10);
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

    static uint16_t fatSectors(uint32_t dataClusters) {
        const uint32_t entries = 2 + dataClusters;
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

    // Write a directory entry: 8.3 name (space-padded, uppercased), attribute
    // (0x20 archive for files, 0x10 for directories), first cluster, and size.
    static void writeDirEntry(uint8_t *e, const std::string &name,
                              uint16_t firstCluster, uint32_t size,
                              uint8_t attr = 0x20) {
        std::memset(e, ' ', 11);
        size_t dot = name.find('.');
        std::string base = (dot == std::string::npos) ? name : name.substr(0, dot);
        std::string ext = (dot == std::string::npos) ? "" : name.substr(dot + 1);
        for (size_t i = 0; i < base.size() && i < 8; ++i)
            e[i] = static_cast<uint8_t>(toupper(base[i]));
        for (size_t i = 0; i < ext.size() && i < 3; ++i)
            e[8 + i] = static_cast<uint8_t>(toupper(ext[i]));
        e[0x0B] = attr;
        // Stamp a fixed build date/time (2026-01-01 00:00) so host-built disks
        // show a real date in CATALOG rather than "(no date)". FAT date word =
        // (year-1980)<<9 | month<<5 | day; time word = 0 (midnight).
        const uint16_t kBuildDate = (46 << 9) | (1 << 5) | 1; // 2026-01-01
        put16(&e[0x0E], 0);         // create time
        put16(&e[0x10], kBuildDate); // create date
        put16(&e[0x12], kBuildDate); // last-access date
        put16(&e[0x16], 0);         // last-write time
        put16(&e[0x18], kBuildDate); // last-write date
        put16(&e[0x1A], firstCluster);
        put32(&e[0x1C], size);
    }

    // Write the '.' (self) and '..' (parent=root) entries at the start of a
    // one-level drawer's directory cluster, matching _DOS_INIT_DRAWER_CLUSTER.
    // The '.'/'..' names are raw (not 8.3), so set the bytes directly.
    static void writeDotEntries(uint8_t *dir, uint16_t selfCluster) {
        std::memset(dir, ' ', 32);
        dir[0] = '.';
        dir[0x0B] = 0x10;                 // directory
        put16(&dir[0x1A], selfCluster);   // '.' -> this drawer's cluster
        put32(&dir[0x1C], 0);
        uint8_t *dd = dir + 32;
        std::memset(dd, ' ', 32);
        dd[0] = '.'; dd[1] = '.';
        dd[0x0B] = 0x10;
        put16(&dd[0x1A], 0);              // '..' -> root
        put32(&dd[0x1C], 0);
    }
};

// ---------------------------------------------------------------------------
// Fat16ImageReader - independently parse a FAT16 image (to verify what the 6502
// write code produced is genuinely valid FAT16, not just round-trippable).
// ---------------------------------------------------------------------------
class Fat16ImageReader {
public:
    struct Entry {
        std::string name; // decoded "NAME.EXT"
        uint32_t size;
        uint16_t firstCluster;
        uint8_t attr;
    };

    explicit Fat16ImageReader(std::vector<uint8_t> image) : img_(std::move(image)) {
        const uint8_t *b = img_.data();
        bytesPerSector_ = rd16(&b[0x0B]);
        sectorsPerCluster_ = b[0x0D];
        const uint16_t reserved = rd16(&b[0x0E]);
        numFats_ = b[0x10];
        rootEntries_ = rd16(&b[0x11]);
        const uint16_t fatSize = rd16(&b[0x16]);
        fatStart_ = reserved;
        rootStart_ = reserved + numFats_ * fatSize;
        const uint16_t rootSectors =
            (rootEntries_ * 32 + bytesPerSector_ - 1) / bytesPerSector_;
        dataStart_ = rootStart_ + rootSectors;
    }

    // Live (non-deleted, non-LFN, non-volume) root directory entries.
    std::vector<Entry> entries() const {
        std::vector<Entry> out;
        const uint8_t *root = &img_[rootStart_ * bytesPerSector_];
        for (uint16_t i = 0; i < rootEntries_; ++i) {
            const uint8_t *e = root + i * 32;
            if (e[0] == 0x00) break;        // end of directory
            if (e[0] == 0xE5) continue;     // deleted
            const uint8_t attr = e[0x0B];
            if ((attr & 0x0F) == 0x0F) continue; // LFN
            if (attr & 0x08) continue;            // volume label
            out.push_back({decodeName(e), rd32(&e[0x1C]), rd16(&e[0x1A]), attr});
        }
        return out;
    }

    bool find(const std::string &name, Entry &out) const {
        for (const auto &e : entries())
            if (e.name == name) { out = e; return true; }
        return false;
    }

    // Read a file's bytes by following its FAT16 cluster chain.
    bool read(const std::string &name, std::vector<uint8_t> &out) const {
        Entry e;
        if (!find(name, e)) return false;
        out.clear();
        uint32_t remaining = e.size;
        uint16_t cluster = e.firstCluster;
        const uint32_t cbytes = bytesPerSector_ * sectorsPerCluster_;
        while (remaining > 0 && cluster >= 2 && cluster < 0xFFF8) {
            const uint32_t lba = dataStart_ + (cluster - 2) * sectorsPerCluster_;
            const uint32_t n = std::min(cbytes, remaining);
            const uint8_t *p = &img_[lba * bytesPerSector_];
            out.insert(out.end(), p, p + n);
            remaining -= n;
            cluster = fatEntry(cluster);
        }
        return remaining == 0;
    }

    // Every file on the image as Fat16File records (name, data, drawer) -- the
    // same shape Fat16ImageBuilder::build consumes, so an image round-trips
    // Reader -> Fat16File[] -> Builder. Root files have drawer=""; files inside a
    // one-level drawer carry that drawer's name.
    std::vector<Fat16File> readAll() const {
        std::vector<Fat16File> out;
        for (const auto &e : entries()) {
            if (e.name == "." || e.name == "..") continue;
            if (e.attr & 0x10) {                        // a drawer (subdirectory)
                for (const auto &f : dirEntries(e.firstCluster)) {
                    if (f.name == "." || f.name == "..") continue;
                    if (f.attr & 0x10) continue;         // one level only
                    out.push_back({f.name, readChain(f.firstCluster, f.size), e.name});
                }
            } else {
                out.push_back({e.name, readChain(e.firstCluster, e.size), ""});
            }
        }
        return out;
    }

    uint16_t fatEntry(uint16_t cluster) const {
        const uint8_t *fat = &img_[fatStart_ * bytesPerSector_];
        return rd16(&fat[cluster * 2]);
    }

    // Count clusters marked allocated (non-zero) from cluster 2 up.
    int allocatedClusters() const {
        int n = 0;
        const uint32_t total = (img_.size() / bytesPerSector_ - dataStart_) / sectorsPerCluster_;
        for (uint32_t c = 2; c < total + 2; ++c)
            if (fatEntry(static_cast<uint16_t>(c)) != 0x0000) ++n;
        return n;
    }

private:
    // Read a cluster chain's bytes (used for files and drawer directories).
    std::vector<uint8_t> readChain(uint16_t cluster, uint32_t size) const {
        std::vector<uint8_t> out;
        const uint32_t cbytes = bytesPerSector_ * sectorsPerCluster_;
        while (size > 0 && cluster >= 2 && cluster < 0xFFF8) {
            const uint32_t lba = dataStart_ + (cluster - 2) * sectorsPerCluster_;
            const uint32_t n = std::min(cbytes, size);
            const uint8_t *p = &img_[lba * bytesPerSector_];
            out.insert(out.end(), p, p + n);
            size -= n;
            cluster = fatEntry(cluster);
        }
        return out;
    }

    // Live entries in a subdirectory (drawer), following its cluster chain.
    std::vector<Entry> dirEntries(uint16_t firstCluster) const {
        std::vector<Entry> out;
        const uint32_t cbytes = bytesPerSector_ * sectorsPerCluster_;
        uint16_t cluster = firstCluster;
        while (cluster >= 2 && cluster < 0xFFF8) {
            const uint32_t lba = dataStart_ + (cluster - 2) * sectorsPerCluster_;
            const uint8_t *dir = &img_[lba * bytesPerSector_];
            for (uint32_t i = 0; i < cbytes; i += 32) {
                const uint8_t *e = dir + i;
                if (e[0] == 0x00) return out;            // end of directory
                if (e[0] == 0xE5) continue;              // deleted
                const uint8_t attr = e[0x0B];
                if ((attr & 0x0F) == 0x0F) continue;     // LFN
                if (attr & 0x08) continue;               // volume label
                out.push_back({decodeName(e), rd32(&e[0x1C]), rd16(&e[0x1A]), attr});
            }
            cluster = fatEntry(cluster);
        }
        return out;
    }

    static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
    static uint32_t rd32(const uint8_t *p) {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    static std::string decodeName(const uint8_t *e) {
        std::string base, ext;
        for (int i = 0; i < 8; ++i) if (e[i] != ' ') base.push_back(static_cast<char>(e[i]));
        for (int i = 8; i < 11; ++i) if (e[i] != ' ') ext.push_back(static_cast<char>(e[i]));
        return ext.empty() ? base : base + "." + ext;
    }

    std::vector<uint8_t> img_;
    uint16_t bytesPerSector_ = 512;
    uint8_t sectorsPerCluster_ = 1;
    uint8_t numFats_ = 1;
    uint16_t rootEntries_ = 0;
    uint16_t fatStart_ = 0, rootStart_ = 0, dataStart_ = 0;
};

} // namespace mfcdos_test

#endif // MFCDOS_TEST_FAT16_IMAGE_H
