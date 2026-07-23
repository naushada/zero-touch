# syntax=docker/dockerfile:1
#
# Build the aarch64 (ARMv8-A) zero-touchd SysV deploy artifact in a container.
#
# Native arm64 build (via QEMU/binfmt), so the daemon's deps come from Debian's
# arm64 packages instead of a hand-rolled cross sysroot: ACE (libace-dev),
# protobuf, libevent, nghttp2, lua, openssl, zlib. Produces a tarball rooted at
# the device filesystem containing the binary + the SysV init script + the
# ds schema + the env file (and the systemd unit, harmless on a SysV box).
#
# Build + export to the host's current directory:
#   ./docker-build.sh            # → ./zero-touchd-aarch64-sysv.tar.gz
#                                # (auto-sets up QEMU binfmt + a buildx builder)
# or directly (requires arm64 binfmt already registered — otherwise the first
# RUN fails with "exec format error"):
#   docker run --privileged --rm tonistiigi/binfmt --install arm64
#   docker buildx build --platform linux/arm64 \
#     --target export --output type=local,dest=. .
#
# The tarball is NOT copied onto a running (read-only) device — extract it into
# your image rootfs, or into /run for a tmpfs smoke test. See DEPLOY.md.

ARG DEBIAN=debian:bookworm-slim
ARG ARTIFACT=zero-touchd-aarch64-sysv.tar.gz

# ── build stage (runs as arm64) ──────────────────────────────────────────────
FROM ${DEBIAN} AS build
ARG DEBIAN_FRONTEND=noninteractive
ARG ARTIFACT

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates file pkg-config \
        libace-dev \
        libssl-dev libssl3 \
        libprotobuf-dev protobuf-compiler \
        libevent-dev libnghttp2-dev \
        liblua5.4-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Show exactly which OpenSSL/ACE objects landed (so the build log is ground
# truth if anything is off), then recreate any missing `lib<name>.so` dev
# symlink from the runtime object — this fixes the linker's `-l<name>` for ACE
# and friends. (OpenSSL itself is pinned explicitly at the cmake step below, so
# it no longer depends on find_library succeeding.)
RUN set -eux; \
    echo "=== openssl packages ==="; (dpkg -l | grep -Ei 'libssl|openssl' || true); \
    echo "=== openssl/ace objects on disk ==="; \
    find /usr/lib /lib -maxdepth 2 \
        \( -name 'libcrypto.so*' -o -name 'libssl.so*' -o -name 'libACE*.so*' \) \
        2>/dev/null | sort || true; \
    for d in /usr/lib "/usr/lib/$(gcc -dumpmachine)"; do \
        [ -d "$d" ] || continue; \
        for real in $(find "$d" -maxdepth 1 -name '*.so.*' 2>/dev/null | sort); do \
            stem="$(basename "$real")"; stem="${stem%%.so.*}"; \
            [ -e "${d}/${stem}.so" ] || ln -s "$real" "${d}/${stem}.so"; \
        done; \
    done; \
    ldconfig

WORKDIR /src
# The build context must already contain the submodule working trees
# (git submodule update --init --recursive on the host — see .dockerignore).
COPY . .

# Configure + build the daemon (+ its reused smsctl/datastore/gnmi_client) and
# stage the install tree into /out. ACE_ROOT=/usr → Debian's libace-dev layout;
# install prefix /usr (device layout, not /usr/local).
# Pin OpenSSL explicitly (headers + the actual crypto/ssl objects) so configure
# does not depend on FindOpenSSL's find_library search, which mis-fires in this
# image (finds the headers → "found version 3.0.x" but not OPENSSL_CRYPTO_LIBRARY).
# The paths are discovered on disk; if either is empty the echo makes it obvious.
# CMAKE_LIBRARY_ARCHITECTURE is the root fix: this image doesn't set it, so
# find_library never searched /usr/lib/<triplet> (multiarch) where every lib
# lives — which is why OpenSSL/Protobuf/Lua "found version" but "missing library".
# Setting it repairs all find_library calls at once. OpenSSL + Protobuf are also
# pinned explicitly as belt-and-suspenders.
RUN set -eux; \
    ARCH="$(gcc -dumpmachine)"; \
    CRYPTO="$(find /usr/lib /lib -name 'libcrypto.so*' 2>/dev/null | sort | tail -1)"; \
    SSL="$(find /usr/lib /lib -name 'libssl.so*' 2>/dev/null | sort | tail -1)"; \
    PROTOBUF="$(find /usr/lib /lib -name 'libprotobuf.so*' 2>/dev/null | sort | tail -1)"; \
    PROTOC="$(command -v protoc || echo /usr/bin/protoc)"; \
    echo "libs: arch=$ARCH crypto=${CRYPTO:-MISSING} protobuf=${PROTOBUF:-MISSING}"; \
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DZT_BUILD_DAEMON=ON \
        -DZT_INSTALL_SYSV=ON \
        -DZT_BUILD_TESTS=OFF \
        -DZT_BUILD_SIM=OFF \
        -DZT_SYSTEMD_DIR=/lib/systemd/system \
        -DACE_ROOT=/usr \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_LIBRARY_ARCHITECTURE="$ARCH" \
        -DOPENSSL_ROOT_DIR=/usr \
        -DOPENSSL_INCLUDE_DIR=/usr/include \
        ${CRYPTO:+-DOPENSSL_CRYPTO_LIBRARY="$CRYPTO"} \
        ${SSL:+-DOPENSSL_SSL_LIBRARY="$SSL"} \
        -DProtobuf_INCLUDE_DIR=/usr/include \
        -DProtobuf_PROTOC_EXECUTABLE="$PROTOC" \
        ${PROTOBUF:+-DProtobuf_LIBRARY="$PROTOBUF"} \
    && cmake --build build -j"$(nproc)" \
    && DESTDIR=/out cmake --install build

# The shipped unit / init script default to /usr/local/bin (host layout); the
# staged binary is /usr/bin. Rewrite the ExecStart / DAEMON path to match.
RUN sed -i 's|/usr/local/bin/zero-touchd|/usr/bin/zero-touchd|' \
        /out/lib/systemd/system/zero-touchd.service \
        /out/etc/init.d/zero-touchd \
    && file /out/usr/bin/zero-touchd

# Package the rootfs-rooted tree (./usr/bin, ./etc/iot, ./etc/init.d, …).
RUN tar czf "/${ARTIFACT}" -C /out .

# ── export stage — only the artifact, for `--output type=local` ──────────────
FROM scratch AS export
ARG ARTIFACT
COPY --from=build /${ARTIFACT} /
