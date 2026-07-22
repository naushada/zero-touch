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
# or directly:
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
        build-essential cmake git ca-certificates file \
        libace-dev \
        libprotobuf-dev protobuf-compiler \
        libevent-dev libnghttp2-dev \
        liblua5.4-dev libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
# The build context must already contain the submodule working trees
# (git submodule update --init --recursive on the host — see .dockerignore).
COPY . .

# Configure + build the daemon (+ its reused smsctl/datastore/gnmi_client) and
# stage the install tree into /out. ACE_ROOT=/usr → Debian's libace-dev layout;
# install prefix /usr (device layout, not /usr/local).
RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DZT_BUILD_DAEMON=ON \
        -DZT_INSTALL_SYSV=ON \
        -DZT_BUILD_TESTS=OFF \
        -DZT_BUILD_SIM=OFF \
        -DZT_SYSTEMD_DIR=/lib/systemd/system \
        -DACE_ROOT=/usr \
        -DCMAKE_INSTALL_PREFIX=/usr \
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
