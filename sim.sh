#!/usr/bin/env bash
#
# sim.sh — run the zero-touch simulation.
#
# Two shapes, both driving the REAL command path (tokenise → parse → session/auth
# → gnmi/classic executor → reply); only the SMS transport is the console.
#
#   ./sim.sh            CLI only. gNMI is an in-memory tree in the same process
#                       — no sockets. Fastest way to exercise the grammar.
#
#   ./sim.sh --wire     CLI + a real gNMI server (zt-gnmi-simd) as two
#                       containers, with gRPC over a bridge network between
#                       them. This is the one that exercises LocalGnmiSink:
#                       protobuf path/TypedValue codecs, RBAC via prefix.target,
#                       and response decoding. Run `<engine> logs -f zt-gnmid`
#                       in another terminal to watch the server side.
#
# Runs on podman or docker — podman is preferred when both are installed; set
# CONTAINER_ENGINE=docker to force the other one.
#
# Usage:
#   ./sim.sh [--wire] [--rebuild] [ARGS…]
#     --wire      two-service mode (see above)
#     --rebuild   rebuild the image first
#     ARGS        override the container command (e.g. `./sim.sh sh`)
#
# Then type SMS bodies at the `>` prompt, e.g.:
#   IOT LOGIN admin admin
#   IOT GNMI GET /system/config/hostname
# REPL commands: /from <num> /enable /disable /allow <csv> /tree /users /help /quit

set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

IMG="zerotouch-sim:local"
COMPOSE_FILE="sim/compose.yml"

WIRE=0
REBUILD=0
while [ $# -gt 0 ]; do
    case "$1" in
        --wire)    WIRE=1;    shift ;;
        --rebuild) REBUILD=1; shift ;;
        *)         break ;;
    esac
done

# Container engine: honour $CONTAINER_ENGINE, else prefer podman, else docker.
ENGINE="${CONTAINER_ENGINE:-}"
if [ -z "$ENGINE" ]; then
    if   command -v podman >/dev/null 2>&1; then ENGINE=podman
    elif command -v docker >/dev/null 2>&1; then ENGINE=docker
    fi
fi
if [ -z "$ENGINE" ] || ! command -v "$ENGINE" >/dev/null 2>&1; then
    echo "sim.sh: no container engine found (looked for podman, then docker)." >&2
    echo "        Set CONTAINER_ENGINE=<engine>, or build natively:" >&2
    echo "        cmake -S . -B build -DZT_BUILD_SIM=ON && ./build/zerotouch-sim" >&2
    exit 1
fi

if [ "$REBUILD" = 1 ] || ! "$ENGINE" image inspect "$IMG" >/dev/null 2>&1; then
    CONTAINER_ENGINE="$ENGINE" ./build.sh --sim
fi

if [ "$WIRE" = 1 ]; then
    if ! "$ENGINE" compose version >/dev/null 2>&1; then
        cat >&2 <<EOF
sim.sh: \`$ENGINE compose\` is unavailable.
        (podman: install podman-compose or the compose provider it delegates to;
         docker: install the Compose v2 plugin.)

Without it, run the two containers by hand:

  $ENGINE network create ztnet 2>/dev/null || true
  $ENGINE run -d --rm --name zt-gnmid --network ztnet -p 50051:50051 \\
      -v "\$PWD/sim/gnmi-tree.lua:/etc/zerotouch/gnmi-tree.lua:ro" \\
      $IMG zt-gnmi-simd --listen=0.0.0.0:50051
  $ENGINE run --rm -it --network ztnet $IMG \\
      zerotouch-sim --gnmi=zt-gnmid:50051
  $ENGINE rm -f zt-gnmid
EOF
        exit 1
    fi

    echo "==> starting the gNMI server (zt-gnmid)…"
    "$ENGINE" compose -f "$COMPOSE_FILE" up -d gnmid

    echo "==> server log:  $ENGINE logs -f zt-gnmid"
    echo "==> attaching the SMS CLI…"
    # `run` (not `up`) so the REPL owns this terminal; --rm drops the CLI
    # container on exit. The server keeps running until the trap fires.
    trap 'echo "==> stopping the gNMI server…"; "$ENGINE" compose -f "$COMPOSE_FILE" down' EXIT
    "$ENGINE" compose -f "$COMPOSE_FILE" run --rm cli "$@"
    exit 0
fi

# Single-container mode: -it gives the REPL a TTY; --rm cleans up on exit.
exec "$ENGINE" run --rm -it "$IMG" "$@"
