#!/bin/bash

PROJECT_ROOT="$(pwd)"
WORKING_DIRECOTRY="$PROJECT_ROOT/game"
BUILD_DIR="$PROJECT_ROOT/build"
BUILD_TYPE="Debug"

build() {
    echo "[INFO] Creating build files..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" || exit 1
    cmake -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_C_STANDARD=99 "$PROJECT_ROOT"
    cmake --build .
    cp compile_commands.json ..
}

run() {
    echo "[INFO] running..."
    cd ${WORKING_DIRECOTRY}
    "./program"
}

clean() {
    echo "[INFO] Cleaning build directory..."
    rm -rf "$BUILD_DIR"
}

case "$1" in
    build)
        build
        ;;
    run)
        run
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

