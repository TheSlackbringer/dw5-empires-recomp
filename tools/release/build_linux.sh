#!/usr/bin/env bash
# =============================================================================
# build_linux.sh - Build the standalone self-building Linux AppImage
# =============================================================================
# Builds the native setup wizard/launcher, exports the relocatable Linux SDK,
# and packages the clean-room distribution AppImage into dist/.
# Zero game data or generated C++ code is required or packaged.
# =============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${DW5E_LINUX_BUILD_DIR:-$REPO_ROOT/app/out/build/linux-amd64-relwithdebinfo}"
DIST_DIR="${DW5E_DIST_DIR:-$REPO_ROOT/dist}"
SDK_DIR="${DW5E_SDK_DIR:-$DIST_DIR/sdk}"

RUN_TESTS=false
TEST_ISO=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test)
            RUN_TESTS=true
            shift
            ;;
        --iso)
            TEST_ISO="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Usage: $0 [--test] [--iso <path/to/dw5e.iso>]" >&2
            exit 1
            ;;
    esac
done

echo "==> Configuring DW5E Linux setup wizard..."
cmake -S "$REPO_ROOT/app" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/tools/linux/clang.cmake"

echo "==> Building setup wizard and test binaries..."
cmake --build "$BUILD_DIR" --target dw5e_setup dw5e_setup_test -j"$(nproc)"

if [ "$RUN_TESTS" = true ]; then
    echo "==> Running unit tests..."
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if [ ! -f "$SDK_DIR/bin/clang++" ]; then
    echo "==> Exporting relocatable Linux SDK..."
    python3 "$REPO_ROOT/tools/release/export_linux_sdk.py" --output "$SDK_DIR"
fi

echo "==> Creating clean-room Linux AppImage package..."
python3 "$REPO_ROOT/tools/release/make_release.py" linux \
    --build "$BUILD_DIR" \
    --linux-sdk "$SDK_DIR"

if [ -n "$TEST_ISO" ]; then
    APPIMAGE=$(find "$DIST_DIR" -maxdepth 1 -name "DW5E-*-linux-x86_64.AppImage" | head -n 1)
    if [ -z "$APPIMAGE" ]; then
        echo "Error: built AppImage not found in $DIST_DIR" >&2
        exit 1
    fi
    echo "==> Testing package headlessly with $TEST_ISO..."
    TEST_DATA_DIR="$(mktemp -d -t dw5e_test_XXXXXX)"
    trap 'rm -rf "$TEST_DATA_DIR"' EXIT
    DW5E_DATA_DIR="$TEST_DATA_DIR" "$APPIMAGE" --install --iso "$TEST_ISO" --no-window
    echo "==> AppImage test installation PASSED!"
fi

echo "==> Linux build and packaging completed successfully!"
