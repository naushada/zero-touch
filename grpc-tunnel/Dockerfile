# syntax=docker/dockerfile:1
#
# One image, three binaries — the server and client containers differ only in
# which one run.sh starts. Debian bookworm carries gRPC 1.51 and protobuf 3.21
# as packages, so nothing is built from source and the image builds in about a
# minute instead of half an hour.
#
#   ./run.sh build     builds this
#   ./run.sh server    tunnel-server  (reachable side)
#   ./run.sh client    tunnel-client  (side behind NAT)

FROM debian:bookworm-slim AS build
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config ca-certificates \
        protobuf-compiler protobuf-compiler-grpc \
        libprotobuf-dev libgrpc++-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY proto ./proto
COPY src ./src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j "$(nproc)" \
    && cmake --install build --prefix /out

# Runtime stage: the shared libraries only. The `|| apt-get install …-dev`
# fallback keeps the image building if Debian bumps the ABI package names.
FROM debian:bookworm-slim
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && ( \
        apt-get install -y --no-install-recommends libgrpc++1.51 libprotobuf32 \
        || apt-get install -y --no-install-recommends libgrpc++-dev libprotobuf-dev \
    ) && rm -rf /var/lib/apt/lists/*

COPY --from=build /out/bin/ /usr/local/bin/

# Overridden by run.sh; the server is the sensible default because it is the
# side that has to be up first.
CMD ["tunnel-server"]
