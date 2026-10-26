#!/usr/bin/env bash
# Clones ReXGlue SDK at pinned release v0.10.0 (c94f5eb) and applies our runtime patches.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="${ROOT_DIR}/rexglue-sdk"
PINNED_COMMIT="c94f5ebdcb3c9d1a460ca48e04f9758448f8d518" # v0.10.0

if [ ! -d "${SDK_DIR}" ]; then
    echo "==> Cloning ReXGlue SDK into ${SDK_DIR}..."
    git clone https://github.com/rexglue/rexglue-sdk.git "${SDK_DIR}"
    git -C "${SDK_DIR}" checkout "${PINNED_COMMIT}"
else
    echo "==> Existing rexglue-sdk directory found at ${SDK_DIR}"
fi

echo "==> Applying runtime patches..."
for patch in "${ROOT_DIR}"/patches/rexglue/*.patch; do
    if [ -f "$patch" ]; then
        echo "Applying $(basename "$patch")..."
        git -C "${SDK_DIR}" apply --check "$patch" 2>/dev/null && git -C "${SDK_DIR}" apply "$patch" || {
            echo "Patch $(basename "$patch") already applied or failed."
        }
    fi
done

echo "==> ReXGlue SDK prepared successfully."
