#!/bin/bash

PROJECT_ROOT="$(pwd)"
WORKING_DIRECOTRY="$PROJECT_ROOT/game"
NATIVE_DIR="$PROJECT_ROOT/build"
WEB_DIR="$PROJECT_ROOT/build_web"
BUILD_TYPE="Debug"

build() {
    echo "[INFO] Creating build files..."

    cmake -S ${PROJECT_ROOT} -B ${NATIVE_DIR} -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_C_STANDARD=99
    bear --append -- cmake --build ${NATIVE_DIR}

    emcmake cmake -S ${PROJECT_ROOT} -B ${WEB_DIR} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_C_STANDARD=99
    bear --append -- cmake --build ${WEB_DIR}
}

clean() {
    echo "[INFO] Cleaning build directory..."
    rm -rf "$BUILD_DIR"
}

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

