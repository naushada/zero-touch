#!/usr/bin/env bash
#
# build.sh — cross-build zero-touchd (+ the SysV init script and packaging) for
# an ARMv8-A / aarch64 device, and stage a deployable tarball.
#
# The daemon links the device's ACE, protobuf, libevent, libevent_openssl,
# nghttp2, lua and openssl. Those come from EITHER a Yocto SDK sysroot
# (recommended) or a plain aarch64 cross toolchain + sysroot you point us at.
#
# Usage:
#   # Yocto SDK (recommended — deps + toolchain all from the SDK):
#   ./build.sh --sdk /opt/poky/<ver>/environment-setup-cortexa53-crypto-poky-linux
#
#   # Plain cross toolchain + a target sysroot:
#   ./build.sh --sysroot /path/to/aarch64-sysroot [--prefix aarch64-linux-gnu-]
#
# Options:
#   --sdk <env-script>     Yocto SDK environment-setup script to source.
#   --sysroot <dir>        Target sysroot (plain-toolchain path).
#   --prefix <tuple->      Cross-compiler prefix (default aarch64-linux-gnu-).
#   --ace-root <dir>       ACE_ROOT in the sysroot (default: <sysroot>/usr).
#   --build-dir <dir>      CMake build dir (default build-aarch64).
#   --dest <dir>           Install staging dir (default dist/aarch64).
#   --jobs <n>             Parallel build jobs (default: nproc).
#   -h | --help            This help.
#
# Output: $DEST is a install tree rooted at the device's filesystem, plus
#         zero-touchd-aarch64.tar.gz next to it. See "Deploy" in DEPLOY.md.

set -euo pipefail

SDK_ENV=""
SYSROOT=""
PREFIX="aarch64-linux-gnu-"
ACE_ROOT=""
BUILD_DIR="build-aarch64"
DEST="dist/aarch64"
JOBS="$( (command -v nproc >/dev/null && nproc) || echo 4)"
PREFIX_INSTALL="/usr/local"          # CMAKE_INSTALL_PREFIX on the device

die() { echo "build.sh: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --sdk)       SDK_ENV="$2"; shift 2 ;;
        --sysroot)   SYSROOT="$2"; shift 2 ;;
        --prefix)    PREFIX="$2"; shift 2 ;;
        --ace-root)  ACE_ROOT="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --dest)      DEST="$2"; shift 2 ;;
        --jobs)      JOBS="$2"; shift 2 ;;
        -h|--help)   sed -n '2,40p' "$0"; exit 0 ;;
        *)           die "unknown option '$1' (see --help)" ;;
    esac
done

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

# The daemon needs iot's nlohmann/json (lookup_account) + smsctl + gnmi_client.
if [ ! -f third_party/iot/apps/3rdparty/json/single_include/nlohmann/json.hpp ]; then
    echo "==> initialising submodules (recursive)…"
    git submodule update --init --recursive
fi

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DZT_BUILD_DAEMON=ON        # implies ZT_BUILD_GNMI + ZT_BUILD_DS
    -DZT_INSTALL_SYSV=ON        # install /etc/init.d/zero-touchd too
    -DZT_BUILD_TESTS=OFF
    -DZT_BUILD_SIM=OFF
    -DZT_SYSTEMD_DIR=/lib/systemd/system
    -DCMAKE_INSTALL_PREFIX="${PREFIX_INSTALL}"
)

if [ -n "$SDK_ENV" ]; then
    # ── Yocto SDK path ──────────────────────────────────────────────────────
    [ -r "$SDK_ENV" ] || die "SDK env script not readable: $SDK_ENV"
    echo "==> sourcing Yocto SDK: $SDK_ENV"
    # shellcheck disable=SC1090
    . "$SDK_ENV"
    # The SDK exports CC/CXX/CMAKE_TOOLCHAIN_FILE (OEToolchainConfig) + the
    # target sysroot. ACE lives under the sysroot's /usr.
    : "${OECORE_TARGET_SYSROOT:?SDK did not set OECORE_TARGET_SYSROOT}"
    ACE_ROOT="${ACE_ROOT:-${OECORE_TARGET_SYSROOT}/usr}"
    CMAKE_ARGS+=( -DACE_ROOT="${ACE_ROOT}" )
    # $CMAKE_TOOLCHAIN_FILE is set by the SDK env; honour it if present.
    if [ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]; then
        CMAKE_ARGS+=( -DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE}" )
    fi
elif [ -n "$SYSROOT" ]; then
    # ── Plain cross toolchain + sysroot path ────────────────────────────────
    [ -d "$SYSROOT" ] || die "sysroot not a directory: $SYSROOT"
    command -v "${PREFIX}g++" >/dev/null 2>&1 || die "compiler not found: ${PREFIX}g++"
    ACE_ROOT="${ACE_ROOT:-${SYSROOT}/usr}"
    CMAKE_ARGS+=(
        -DCMAKE_TOOLCHAIN_FILE="${REPO}/cmake/aarch64-toolchain.cmake"
        -DZT_TARGET_SYSROOT="${SYSROOT}"
        -DZT_TOOLCHAIN_PREFIX="${PREFIX}"
        -DACE_ROOT="${ACE_ROOT}"
        # pkg-config must look inside the sysroot for libevent/nghttp2/protobuf.
        -DPKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON
    )
    export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"
    export PKG_CONFIG_LIBDIR="${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig"
else
    die "specify --sdk <env-script> or --sysroot <dir> (see --help)"
fi

echo "==> configuring ($BUILD_DIR)…"
cmake -S . -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"

echo "==> building (-j$JOBS)…"
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "==> staging install → $DEST"
rm -rf "$DEST"
DESTDIR="${REPO}/${DEST}" cmake --install "$BUILD_DIR"

BIN="${DEST}${PREFIX_INSTALL}/bin/zero-touchd"
if command -v file >/dev/null 2>&1 && [ -f "$BIN" ]; then
    echo "==> arch check: $(file -b "$BIN")"
    case "$(file -b "$BIN")" in
        *aarch64*|*ARM\ aarch64*) : ;;
        *) echo "    WARNING: binary is not aarch64 — check your toolchain/SDK." >&2 ;;
    esac
fi

TARBALL="zero-touchd-aarch64.tar.gz"
echo "==> packaging → $TARBALL"
tar czf "$TARBALL" -C "$DEST" .

echo
echo "Done. Staged tree: $DEST"
echo "Deploy tarball:    $TARBALL   (rooted at the device filesystem)"
echo "Next: scp it to the device and follow 'Deploy to an aarch64 device' in DEPLOY.md."
