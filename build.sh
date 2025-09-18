#!/bin/bash

# 6502 Kernel Build Script
# Builds the project using out-of-source CMake configuration

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

BUILD_DIR="cmake-build-debug"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

echo -e "${YELLOW}6502 Kernel Build Script${NC}"
echo "========================================="

# Change to script directory
cd "$SCRIPT_DIR"

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir -p "$BUILD_DIR"
fi

# Configure if needed
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo -e "${YELLOW}Configuring CMake...${NC}"
    cd "$BUILD_DIR"
    cmake -G Ninja ..
    cd ..
fi

# Build
echo -e "${YELLOW}Building kernel...${NC}"
cd "$BUILD_DIR"
ninja

# Success message
echo -e "${GREEN}Build completed successfully!${NC}"
echo "========================================="
echo "Kernel ROM: $BUILD_DIR/kernel.rom"
echo "Executable: $BUILD_DIR/bin/6502-kernel"