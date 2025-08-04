#!/bin/bash

# Resolve the directory this script is in and move one level up to get the project root
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# Function to build the project using CMake with C99
build() {
    echo "[INFO] Creating build files..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" || exit 1
    cmake -GNinja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_STANDARD=99 "$PROJECT_ROOT"
}

# Function to clean the build directory
clean() {
    echo "[INFO] Cleaning build directory..."
    rm -rf "$BUILD_DIR"
}

# Main logic
case "$1" in
    build)
        build
        ;;
    clean)
        clean
        ;;
    "")
        build
        ;;
    *)
        echo "Usage: $0 {build|clean}"
        ;;
esac

