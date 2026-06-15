// mkfat16 - create a FAT16 disk.img for the MFC-DOS block device.
//
// Usage:
//   mkfat16 <output.img> [hostfile ...]
//
// Each hostfile is copied in under an 8.3 name derived from its basename
// (uppercased, sanitized, truncated to 8.3). With no hostfiles, a couple of
// sample text files are written so there is something to CATALOG / TYPE.
//
// The image it writes is the same FAT16 layout the resident driver in
// src/kernel/dos/dos.asm reads. The builder header is shared with the
// filesystem tests (it originated there); see fat16_image.h.

#include "fat16_image.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;

namespace {

// Derive an 8.3 FAT name ("NAME.EXT") from a host path's basename.
std::string to83(const std::string &path) {
    const auto slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    std::string name = (dot == std::string::npos) ? base : base.substr(0, dot);
    std::string ext = (dot == std::string::npos) ? "" : base.substr(dot + 1);

    auto clean = [](const std::string &s, size_t n) {
        std::string out;
        for (char c : s) {
            if (out.size() >= n) break;
            unsigned char uc = static_cast<unsigned char>(c);
            out += std::isalnum(uc) ? static_cast<char>(std::toupper(uc)) : '_';
        }
        return out;
    };
    name = clean(name, 8);
    ext = clean(ext, 3);
    if (name.empty()) name = "FILE";
    return ext.empty() ? name : name + "." + ext;
}

bool readHostFile(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    if (n > 0) f.read(reinterpret_cast<char *>(out.data()), n);
    return true;
}

std::vector<uint8_t> bytesOf(const std::string &s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output.img> [hostfile ...]\n";
        return 2;
    }
    const std::string outPath = argv[1];

    std::vector<Fat16File> files;
    if (argc == 2) {
        // No host files: write sample text files (CRLF so TYPE shows line breaks).
        files.push_back({"README.TXT",
                         bytesOf("MFC-DOS SAMPLE DISK\r\n"
                                 "TYPE @ TO LIST FILES, @NAME TO PRINT ONE.\r\n")});
        files.push_back({"HELLO.TXT", bytesOf("HELLO FROM MFC-DOS!\r\n")});
        std::cout << "No host files given - writing sample files.\n";
    } else {
        for (int i = 2; i < argc; ++i) {
            std::vector<uint8_t> data;
            if (!readHostFile(argv[i], data)) {
                std::cerr << "error: cannot read '" << argv[i] << "'\n";
                return 1;
            }
            files.push_back({to83(argv[i]), std::move(data)});
        }
    }

    // Size the volume as a genuine FAT16 (>= 4085 clusters) so host OSes mount
    // it as FAT16, not FAT12.
    const std::vector<uint8_t> img =
        Fat16ImageBuilder::build(files, Fat16ImageBuilder::kHostFat16Clusters);
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "error: cannot write '" << outPath << "'\n";
        return 1;
    }
    out.write(reinterpret_cast<const char *>(img.data()),
              static_cast<std::streamsize>(img.size()));
    out.close();

    std::cout << "Wrote " << img.size() << " bytes to " << outPath << " ("
              << files.size() << " file(s)):\n";
    for (const auto &f : files)
        std::cout << "  " << f.name << "  (" << f.data.size() << " bytes)\n";
    return 0;
}
