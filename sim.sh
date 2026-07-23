#!/usr/bin/env bash
#
# sim.sh — run the offline SMS simulator in its container (interactive CLI).
#
# zerotouch-sim bypasses the SMS transport entirely: it wires the REAL Bridge
# (tokenise → parse → session/auth → gnmi/classic executor → reply) behind an
# in-process console transport, so each line you type lands straight at
# Bridge::on_sms — no modem, no ds-server, no gRPC (in-memory gNMI). On entry the
# container shows a SIM banner. See DESIGN.md / README.md.
#
# Usage:
#   ./sim.sh [--rebuild] [ARGS…]
#     --rebuild   rebuild the image first
#     ARGS        override the container command (e.g. `./sim.sh sh` for a shell)
#
# Then type SMS bodies at the `>` prompt, e.g.:
#   IOT LOGIN admin admin
#   IOT GNMI GET /system/config/hostname
# REPL commands: /from <num> /enable /disable /allow <csv> /tree /users /help /quit

set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

IMG="zerotouch-sim:local"

REBUILD=0
[ "${1:-}" = "--rebuild" ] && { REBUILD=1; shift; }

command -v docker >/dev/null 2>&1 || { echo "sim.sh: docker not found" >&2; exit 1; }

if [ "$REBUILD" = 1 ] || ! docker image inspect "$IMG" >/dev/null 2>&1; then
    ./build.sh --sim
fi

# -it gives the REPL a TTY; --rm cleans up on exit. Extra args override CMD.
exec docker run --rm -it "$IMG" "$@"
