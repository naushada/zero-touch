#!/usr/bin/env bash
#
# docker-build.sh — build the aarch64 zero-touchd SysV deploy artifact in a
# container and copy it to the host's current directory (or $1).
#
# The daemon is built NATIVELY as arm64 via QEMU emulation, so this needs:
#   1. QEMU binfmt handlers for arm64 (kernel-level, persists across builds);
#   2. a buildx "docker-container" builder (the reliable driver for an emulated
#      --platform build with --output type=local).
# Both are set up automatically below (the binfmt step needs a one-off
# --privileged helper container). Skip the auto-setup with --no-setup if you
# manage these yourself.
#
# Usage:
#   ./docker-build.sh [OUTPUT_DIR] [--no-setup]
#
# Output: <OUTPUT_DIR>/zero-touchd-aarch64-sysv.tar.gz — a tree rooted at the
# device filesystem (usr/bin/zero-touchd, etc/init.d/zero-touchd, ds schema,
# env, systemd unit). Extract it into your image rootfs, or into /run for a
# tmpfs smoke test. See DEPLOY.md; it is NOT copied onto a running device.

set -euo pipefail

OUT_DIR="."
SETUP=1
for arg in "$@"; do
    case "$arg" in
        --no-setup) SETUP=0 ;;
        -h|--help)  sed -n '2,22p' "$0"; exit 0 ;;
        *)          OUT_DIR="$arg" ;;
    esac
done

ARTIFACT="zero-touchd-aarch64-sysv.tar.gz"
BUILDER="ztbuilder"
REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

die() { echo "docker-build.sh: $*" >&2; exit 1; }
command -v docker >/dev/null 2>&1 || die "docker not found"

# The daemon reuses the iot + grace-server submodules (and iot's nested json).
if [ ! -f third_party/iot/apps/3rdparty/json/single_include/nlohmann/json.hpp ]; then
    echo "==> initialising submodules (recursive)…"
    git submodule update --init --recursive
fi

# ── 1. QEMU arm64 emulation ──────────────────────────────────────────────────
# Without this the arm64 base image cannot exec its own /bin/sh and the first
# RUN dies with "exec format error".
arm64_ready() {
    # A registered, enabled handler for aarch64 (Linux hosts). On Docker Desktop
    # (macOS/Windows) the VM already provides this; the file check is skipped.
    [ ! -d /proc/sys/fs/binfmt_misc ] && return 0
    [ -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ] && \
        grep -q '^enabled' /proc/sys/fs/binfmt_misc/qemu-aarch64 2>/dev/null
}
if [ "$SETUP" = 1 ] && ! arm64_ready; then
    echo "==> registering QEMU arm64 emulation (one-off, --privileged)…"
    docker run --privileged --rm tonistiigi/binfmt --install arm64 \
        || die "binfmt install failed — run it manually, or pass --no-setup once set up"
fi

# ── 2. buildx builder (docker-container driver) ──────────────────────────────
if ! docker buildx inspect "$BUILDER" >/dev/null 2>&1; then
    echo "==> creating buildx builder '$BUILDER' (docker-container)…"
    docker buildx create --name "$BUILDER" --driver docker-container >/dev/null
fi
docker buildx use "$BUILDER"
docker buildx inspect --bootstrap "$BUILDER" >/dev/null

# Fail early with a clear message if arm64 still is not offered.
if ! docker buildx inspect "$BUILDER" | grep -q 'linux/arm64'; then
    die "builder '$BUILDER' does not offer linux/arm64 — register QEMU with:
       docker run --privileged --rm tonistiigi/binfmt --install arm64"
fi

mkdir -p "$OUT_DIR"

echo "==> building aarch64 artifact via buildx → ${OUT_DIR%/}/$ARTIFACT"
docker buildx build --builder "$BUILDER" \
    --platform linux/arm64 \
    --target export \
    --output "type=local,dest=${OUT_DIR}" \
    -f Dockerfile .

echo
echo "Done: ${OUT_DIR%/}/${ARTIFACT}"
echo "Verify: tar tzf '${OUT_DIR%/}/${ARTIFACT}' | head"
