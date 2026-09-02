// mkdisk - build / read / update an MFC-DOS FAT16 disk image from a "diskmap
// bundle": a directory holding a diskmap.txt (one disk-path per line) plus the
// files laid out mirroring the disk. Supersedes mkfat16.
//
//   mkdisk create <image> <diskmap.txt>   build a fresh image from the bundle
//   mkdisk read   <image> <outdir>        extract files + write outdir/diskmap.txt
//   mkdisk update <image> <diskmap.txt>   replace/add the listed files, keep the rest
//
// create/update refuse to write an image a running machine has claimed, because
// the write lands in place and the machine adopts it mid-session; --force
// overrides. See host/ImageLock.h for what goes wrong without this.
//
// A diskmap line is one disk path ("EDIT.PRG", "SYSTEM/DIAL.LST"); a leading
// DRAWER/ names a one-level drawer. '#' comments and blank lines are ignored.
// For create/update, each path's bytes come from <diskmap-dir>/<path> (the tool
// does no searching -- the bundle already holds the files). The image layout the
// tool produces is the same FAT16 the resident driver reads; see fat16_image.h.

#include "fat16_image.h"
#include "host/ImageLock.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;
using mfcdos_test::Fat16ImageReader;

namespace {

std::string upper(std::string s) {
    for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool readFileBytes(const fs::path &p, std::vector<uint8_t> &out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    f.seekg(0);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    if (n > 0) f.read(reinterpret_cast<char *>(out.data()), n);
    return true;
}

// Refuse to rewrite an image a running machine is using.
//
// Checked before the build rather than after, so a refusal costs nothing and
// leaves the existing image untouched. A missing image acquires trivially --
// nothing can be using a file that is not there.
bool claimForWrite(const std::string &path, bool force, Host::ImageLock &lock) {
    if (lock.tryAcquire(path, Host::LockMode::Exclusive)) return true;
    if (force) {
        std::cerr << "mkdisk: " << path << " is in use by a running machine; "
                  << "overwriting anyway (--force)\n";
        return true;
    }
    std::cerr << "mkdisk: " << path << " is in use by a running machine, so it was "
                 "NOT rewritten.\n"
                 "        Quit the emulator and run this again, or pass --force to "
                 "overwrite it anyway\n"
                 "        (a machine still holding it can then corrupt the new "
                 "image).\n";
    return false;
}

bool writeImage(const std::string &path, const std::vector<Fat16File> &files,
                bool force) {
    Host::ImageLock lock;
    if (!claimForWrite(path, force, lock)) return false;

    const std::vector<uint8_t> img =
        Fat16ImageBuilder::build(files, Fat16ImageBuilder::kHostFat16Clusters);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char *>(img.data()),
              static_cast<std::streamsize>(img.size()));
    return static_cast<bool>(out);
}

// Read an image file and hand back a reader (the Reader parses the boot sector in
// its ctor, so it must only be constructed from a real >=512-byte image).
bool loadReader(const std::string &path, std::vector<uint8_t> &bytes) {
    if (!readFileBytes(path, bytes) || bytes.size() < 512) return false;
    return true;
}

// "GAMES/CHESS.PRG" -> drawer="GAMES", name="CHESS.PRG"; "EDIT.PRG" -> "","EDIT.PRG".
bool splitDiskPath(const std::string &path, std::string &drawer, std::string &name) {
    const auto slash = path.find('/');
    if (slash == std::string::npos) { drawer.clear(); name = path; }
    else {
        drawer = path.substr(0, slash);
        name = path.substr(slash + 1);
        if (name.find('/') != std::string::npos) return false;  // one level only
    }
    return !name.empty();
}

// Read the bundle's files named by a diskmap into Fat16File records (order kept).
bool loadBundle(const fs::path &mapPath, std::vector<Fat16File> &files) {
    std::ifstream f(mapPath);
    if (!f) { std::cerr << "mkdisk: cannot read diskmap " << mapPath << "\n"; return false; }
    const fs::path base = mapPath.parent_path();
    std::string line;
    while (std::getline(f, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const auto a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        const auto b = line.find_last_not_of(" \t\r\n");
        const std::string path = line.substr(a, b - a + 1);
        std::string drawer, name;
        if (!splitDiskPath(path, drawer, name)) { std::cerr << "mkdisk: bad path '" << path << "'\n"; return false; }
        std::vector<uint8_t> data;
        if (!readFileBytes(base / path, data)) { std::cerr << "mkdisk: missing bundle file " << (base / path) << "\n"; return false; }
        files.push_back({name, std::move(data), drawer});
    }
    return true;
}

int doCreate(const std::string &image, const std::string &mapPath, bool force) {
    std::vector<Fat16File> files;
    if (!loadBundle(mapPath, files)) return 1;
    if (!writeImage(image, files, force)) return 1;
    std::cout << "Created " << image << " (" << files.size() << " files)\n";
    return 0;
}

int doRead(const std::string &image, const std::string &outdir) {
    std::vector<uint8_t> bytes;
    if (!loadReader(image, bytes)) { std::cerr << "mkdisk: cannot read " << image << "\n"; return 1; }
    Fat16ImageReader reader(std::move(bytes));
    const std::vector<Fat16File> files = reader.readAll();
    std::error_code ec;
    fs::create_directories(outdir, ec);
    std::ofstream map(fs::path(outdir) / "diskmap.txt", std::ios::trunc);
    if (!map) { std::cerr << "mkdisk: cannot write diskmap in " << outdir << "\n"; return 1; }
    map << "# mkdisk diskmap: one disk-path per line (DRAWER/NAME = a drawer)\n";
    for (const auto &f : files) {
        const std::string diskpath = f.drawer.empty() ? f.name : (f.drawer + "/" + f.name);
        const fs::path outfile = fs::path(outdir) / diskpath;
        fs::create_directories(outfile.parent_path(), ec);
        std::ofstream of(outfile, std::ios::binary | std::ios::trunc);
        of.write(reinterpret_cast<const char *>(f.data.data()),
                 static_cast<std::streamsize>(f.data.size()));
        map << diskpath << "\n";
    }
    std::cout << "Read " << files.size() << " files -> " << outdir << "\n";
    return 0;
}

int doUpdate(const std::string &image, const std::string &mapPath, bool force) {
    std::vector<uint8_t> bytes;
    if (!loadReader(image, bytes)) { std::cerr << "mkdisk: cannot read " << image << "\n"; return 1; }
    Fat16ImageReader reader(std::move(bytes));
    std::vector<Fat16File> files = reader.readAll();        // existing contents
    std::vector<Fat16File> repl;
    if (!loadBundle(mapPath, repl)) return 1;
    for (auto &nf : repl) {
        bool found = false;
        for (auto &ef : files) {
            if (upper(ef.name) == upper(nf.name) && upper(ef.drawer) == upper(nf.drawer)) {
                ef.data = nf.data; found = true; break;
            }
        }
        if (!found) files.push_back(nf);                    // new file
    }
    if (!writeImage(image, files, force)) return 1;
    std::cout << "Updated " << image << " (" << repl.size() << " file(s))\n";
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    // --force may sit anywhere; everything else is positional.
    bool force = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--force") force = true;
        else args.push_back(a);
    }

    if (args.size() == 3) {
        const std::string &cmd = args[0];
        if (cmd == "create") return doCreate(args[1], args[2], force);
        if (cmd == "read")   return doRead(args[1], args[2]);
        if (cmd == "update") return doUpdate(args[1], args[2], force);
    }
    std::cerr <<
        "usage:\n"
        "  mkdisk create <image> <diskmap.txt>   build a fresh image from the bundle\n"
        "  mkdisk read   <image> <outdir>        extract files + write outdir/diskmap.txt\n"
        "  mkdisk update <image> <diskmap.txt>   replace/add listed files, keep the rest\n"
        "\n"
        "  --force   rewrite the image even if a running machine has it open\n";
    return 2;
}
