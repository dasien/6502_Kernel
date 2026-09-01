// mkprg - prepend the 2-byte little-endian load-address header to a raw ld65
// binary, producing a .PRG that MFC-DOS can launch by name.
//
//   mkprg <load-addr> <in.bin> <out.PRG>       load-addr as hex, e.g. 0800
//
// This is the whole of the .PRG format: two bytes of load address, then the
// image. It used to be `printf '\000\010' > out; cat bin >> out` repeated in
// nine shell scripts, which is why the build needed a POSIX shell. Doing it in
// a host tool costs forty lines and builds anywhere CMake does.

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: mkprg <load-addr-hex> <in.bin> <out.PRG>\n";
        return 2;
    }

    // Parse the load address before touching any file: a typo here silently
    // produces a .PRG that loads to the wrong place and crashes on launch.
    char *end = nullptr;
    const unsigned long addr = std::strtoul(argv[1], &end, 16);
    if (end == argv[1] || *end != '\0' || addr > 0xFFFF) {
        std::cerr << "mkprg: '" << argv[1] << "' is not a 16-bit hex address\n";
        return 2;
    }

    std::ifstream in(argv[2], std::ios::binary);
    if (!in) {
        std::cerr << "mkprg: cannot read " << argv[2] << "\n";
        return 1;
    }
    const std::vector<char> image{std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>()};

    std::ofstream out(argv[3], std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "mkprg: cannot write " << argv[3] << "\n";
        return 1;
    }
    const char header[2] = {static_cast<char>(addr & 0xFF),
                            static_cast<char>((addr >> 8) & 0xFF)};
    out.write(header, 2);
    out.write(image.data(), static_cast<std::streamsize>(image.size()));
    if (!out) {
        std::cerr << "mkprg: write failed for " << argv[3] << "\n";
        return 1;
    }

    std::cout << argv[3] << " (" << image.size() + 2 << " bytes)\n";
    return 0;
}
