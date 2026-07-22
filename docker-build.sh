#!/usr/bin/env bash
#
# docker-build.sh — build the aarch64 zero-touchd SysV deploy artifact in a
# container and copy it to the host's current directory (or $1).
#
# Requires Docker with buildx + QEMU/binfmt for arm64 emulation:
#   docker run --privileged --rm tonistiigi/binfmt --install arm64
#
# Usage:
#   ./docker-build.sh [OUTPUT_DIR]      # default: current directory
#
# Output: <OUTPUT_DIR>/zero-touchd-aarch64-sysv.tar.gz — a tree rooted at the
# device filesystem (usr/bin/zero-touchd, etc/init.d/zero-touchd, ds schema,
# env, systemd unit). Extract it into your image rootfs, or into /run for a
# tmpfs smoke test. See DEPLOY.md; it is NOT copied onto a running device.

set -euo pipefail

OUT_DIR="${1:-.}"
ARTIFACT="zero-touchd-aarch64-sysv.tar.gz"
REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

command -v docker >/dev/null 2>&1 || { echo "docker not found" >&2; exit 1; }

# The daemon reuses the iot + grace-server submodules (and iot's nested json).
if [ ! -f third_party/iot/apps/3rdparty/json/single_include/nlohmann/json.hpp ]; then
    echo "==> initialising submodules (recursive)…"
    git submodule update --init --recursive
fi

mkdir -p "$OUT_DIR"

echo "==> building aarch64 artifact via buildx → $OUT_DIR/$ARTIFACT"
docker buildx build \
    --platform linux/arm64 \
    --target export \
    --output "type=local,dest=${OUT_DIR}" \
    -f Dockerfile .

echo
echo "Done: ${OUT_DIR%/}/${ARTIFACT}"
echo "Verify: tar tzf '${OUT_DIR%/}/${ARTIFACT}' | head"
