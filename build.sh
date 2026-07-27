#!/bin/bash
#
# MFC 6502 build script -- a thin wrapper over the CMake presets.
#
#   ./build.sh                 configure + build + assemble disk.img
#   ./build.sh release         use the 'release' preset instead of 'dev'
#   ./build.sh --fresh         delete the build dir first (see below)
#   ./build.sh --no-disk       skip re-assembling disk.img
#
# --fresh is needed after installing Qt or cc65: CMake caches "not found"
# results, so a plain re-run will keep building without the GUI or without
# sound even once the package is present.
#
# Everything here is also available directly:
#   cmake --preset dev && cmake --build --preset dev
#
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

PRESET="dev"
FRESH=0
BUILD_DISK=1

for arg in "$@"; do
    case "$arg" in
        --fresh)   FRESH=1 ;;
        --no-disk) BUILD_DISK=0 ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)        echo -e "${RED}Unknown option: $arg${NC}"; exit 1 ;;
        *)         PRESET="$arg" ;;
    esac
done

# ----------------------------------------------------------------
# Dependency preflight -- fail with an actionable message, not a stack trace
# ----------------------------------------------------------------
install_hint() {
    if   command -v apt    >/dev/null 2>&1; then
        echo "  sudo apt install build-essential cmake ninja-build cc65 qt6-base-dev qt6-multimedia-dev"
    elif command -v dnf    >/dev/null 2>&1; then
        echo "  sudo dnf install gcc-c++ cmake ninja-build cc65 qt6-qtbase-devel qt6-qtmultimedia-devel"
    elif command -v pacman >/dev/null 2>&1; then
        echo "  sudo pacman -S base-devel cmake ninja cc65 qt6-base qt6-multimedia"
    elif command -v brew   >/dev/null 2>&1; then
        echo "  brew install cmake ninja cc65 qt"
    else
        echo "  (install cmake, ninja, cc65 and Qt6 with your package manager)"
    fi
}

MISSING=()
for tool in cmake ninja ca65 ld65; do
    command -v "$tool" >/dev/null 2>&1 || MISSING+=("$tool")
done

if [ ${#MISSING[@]} -ne 0 ]; then
    echo -e "${RED}Missing required tools: ${MISSING[*]}${NC}"
    echo ""
    echo "Install the full dependency set with:"
    install_hint
    echo ""
    echo "See docs/README.md 'Prerequisites' for what each one provides."
    exit 1
fi

# ----------------------------------------------------------------
# Configure + build
# ----------------------------------------------------------------
echo -e "${YELLOW}MFC 6502 build (preset: $PRESET)${NC}"
echo "========================================="

# Keep in sync with the binaryDir of each preset in CMakePresets.json.
case "$PRESET" in
    dev)     BUILD_DIR="cmake-build-debug" ;;
    debug)   BUILD_DIR="cmake-build-debug-g" ;;
    release) BUILD_DIR="cmake-build-release" ;;
    no-gui)  BUILD_DIR="cmake-build-nogui" ;;
    *)       echo -e "${RED}Unknown preset: $PRESET${NC}"
             echo "Available: dev, debug, release, no-gui  (see CMakePresets.json)"
             exit 1 ;;
esac

if [ "$FRESH" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Removing $BUILD_DIR (--fresh)...${NC}"
    rm -rf "$BUILD_DIR"
fi

cmake --preset "$PRESET"
cmake --build --preset "$PRESET"

if [ "$BUILD_DISK" -eq 1 ]; then
    echo -e "${YELLOW}Assembling disk.img...${NC}"
    cmake --build "$BUILD_DIR" --target disk
fi

# ----------------------------------------------------------------
echo -e "${GREEN}Build completed successfully.${NC}"
echo "========================================="
echo "Executable: $BUILD_DIR/bin/6502-kernel"
echo "ROMs:       $BUILD_DIR/kernel/*.rom"
echo "Disk image: $BUILD_DIR/disk.img"
echo ""
echo "Run it:     cd $BUILD_DIR/bin && ./6502-kernel"
echo "Test it:    ctest --test-dir $BUILD_DIR"
