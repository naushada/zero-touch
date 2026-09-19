#!/usr/bin/env bash
#
# run.sh — build and run the gRPC reverse tunnel demo as two containers.
#
#   ./run.sh build              build the image (detects podman, else docker)
#   ./run.sh server             run the tunnel server   (reachable side)
#   ./run.sh client             run the tunnel client   (side behind NAT)
#   ./run.sh rpc  [MESSAGE]     call the client's Echo service through the
#                               tunnel, from inside the server container
#   ./run.sh demo [MESSAGE]     both containers detached + one rpc, end to end
#   ./run.sh logs [server|client]   follow a container's log
#   ./run.sh stop               remove both containers
#   ./run.sh clean              …and the network and the image
#
# The usual shape is three terminals: `./run.sh server`, `./run.sh client`,
# then `./run.sh rpc hello`. Add -d to server/client to detach instead.
#
# Options:
#   -d, --detach        run the container in the background
#       --stream        (rpc) also exercise the server-streaming RPC
#
# podman is preferred when both engines are installed; set CONTAINER_ENGINE
# to force one.
#
# The two containers reach each other over a private network, so no host port
# is published by default. Set HOST_PORT=50051 to expose the server's dial-in
# port as well, e.g. to run a tunnel client straight on the host.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE="grpc-tunnel:local"
NET="grpc-tunnel-net"
SERVER_CTR="grpc-tunnel-server"
CLIENT_CTR="grpc-tunnel-client"

TUNNEL_PORT=50051   # server: the tunnel clients dial in to this
FORWARD_PORT=50052  # server: loopback TCP the app's gRPC client dials
ECHO_PORT=50060     # client: loopback the app's gRPC server binds
TARGET="edge-1"     # tunnel name the client registers under

# ---------------------------------------------------------------- engine ----

# Honour $CONTAINER_ENGINE, else prefer podman, else docker.
detect_engine() {
    local engine="${CONTAINER_ENGINE:-}"
    if [ -z "$engine" ]; then
        if   command -v podman >/dev/null 2>&1; then engine=podman
        elif command -v docker >/dev/null 2>&1; then engine=docker
        fi
    fi
    if [ -z "$engine" ] || ! command -v "$engine" >/dev/null 2>&1; then
        cat >&2 <<EOF
run.sh: no container engine found (looked for podman, then docker).
        Install one, or set CONTAINER_ENGINE=<engine>.
        To build natively instead:
          cmake -S . -B build && cmake --build build -j
EOF
        exit 1
    fi
    printf '%s' "$engine"
}

ENGINE="$(detect_engine)"

# podman on macOS/Windows needs its VM up before anything else will work.
check_engine_ready() {
    if "$ENGINE" info >/dev/null 2>&1; then return 0; fi
    echo "run.sh: '$ENGINE' is installed but not responding." >&2
    [ "$ENGINE" = podman ] && echo "        Try: podman machine start" >&2
    exit 1
}

ensure_image() {
    if ! "$ENGINE" image exists "$IMAGE" 2>/dev/null &&
       ! "$ENGINE" image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "==> image $IMAGE not found, building it first"
        cmd_build
    fi
}

ensure_net() {
    "$ENGINE" network inspect "$NET" >/dev/null 2>&1 || {
        echo "==> creating network $NET"
        "$ENGINE" network create "$NET" >/dev/null
    }
}

# Containers are named, so a stale one from a previous run would collide.
drop_ctr() { "$ENGINE" rm -f "$1" >/dev/null 2>&1 || true; }

# ------------------------------------------------------------- commands -----

cmd_build() {
    check_engine_ready
    echo "==> engine: $ENGINE"
    echo "==> building $IMAGE"
    "$ENGINE" build -t "$IMAGE" -f Dockerfile .
    echo "==> built $IMAGE"
}

cmd_server() {
    check_engine_ready; ensure_image; ensure_net
    drop_ctr "$SERVER_CTR"

    echo "==> tunnel server: :$TUNNEL_PORT (dial-in), forwarder :$FORWARD_PORT"
    # Publishing is opt-in: the client container dials over $NET, and 50051 is
    # a popular port to already have taken on a dev box.
    local publish=()
    if [ -n "${HOST_PORT:-}" ]; then
        publish=(-p "$HOST_PORT:$TUNNEL_PORT")
        echo "==> publishing dial-in on host port $HOST_PORT"
    fi
    "$ENGINE" run "${RUN_MODE[@]}" --name "$SERVER_CTR" \
        --network "$NET" ${publish[@]+"${publish[@]}"} \
        "$IMAGE" tunnel-server \
            --listen "0.0.0.0:$TUNNEL_PORT" \
            --forward "127.0.0.1:$FORWARD_PORT" \
            --target "$TARGET"

    [ "$DETACH" = 1 ] && echo "==> detached; logs: ./run.sh logs server"
    return 0
}

cmd_client() {
    check_engine_ready; ensure_image; ensure_net
    drop_ctr "$CLIENT_CTR"

    echo "==> tunnel client: dialling $SERVER_CTR:$TUNNEL_PORT as '$TARGET'"
    # No -p on purpose: the Echo service is on loopback inside this container
    # and is reachable only through the tunnel.
    "$ENGINE" run "${RUN_MODE[@]}" --name "$CLIENT_CTR" \
        --network "$NET" \
        "$IMAGE" tunnel-client \
            --tunnel "$SERVER_CTR:$TUNNEL_PORT" \
            --target "$TARGET" \
            --echo "127.0.0.1:$ECHO_PORT"

    [ "$DETACH" = 1 ] && echo "==> detached; logs: ./run.sh logs client"
    return 0
}

# Runs in the SERVER container: dials the forwarder, so the call crosses the
# tunnel to the client container and back.
cmd_rpc() {
    check_engine_ready
    local message="${1:-hello}"
    if ! "$ENGINE" exec "$SERVER_CTR" true >/dev/null 2>&1; then
        echo "run.sh: $SERVER_CTR is not running — start it with ./run.sh server" >&2
        exit 1
    fi
    "$ENGINE" exec "$SERVER_CTR" echo-rpc \
        --addr "127.0.0.1:$FORWARD_PORT" --message "$message" \
        ${STREAM:+--stream} --count 3
}

cmd_demo() {
    local message="${1:-hello}"
    DETACH=1; RUN_MODE=(-d --rm)
    cmd_server
    cmd_client

    echo "==> waiting for the tunnel to register…"
    local i
    for i in $(seq 1 30); do
        "$ENGINE" logs "$CLIENT_CTR" 2>&1 | grep -q "waiting for dial-in" && break
        sleep 1
    done

    echo "==> calling Echo through the tunnel"
    STREAM=1 cmd_rpc "$message"

    echo
    echo "==> server log:"; "$ENGINE" logs "$SERVER_CTR" 2>&1 | tail -8
    echo "==> client log:"; "$ENGINE" logs "$CLIENT_CTR" 2>&1 | tail -8
    echo
    echo "==> still running. ./run.sh rpc <msg> for more, ./run.sh stop to finish."
}

cmd_logs() {
    check_engine_ready
    case "${1:-server}" in
        server) "$ENGINE" logs -f "$SERVER_CTR" ;;
        client) "$ENGINE" logs -f "$CLIENT_CTR" ;;
        *) echo "run.sh: logs takes 'server' or 'client'" >&2; exit 1 ;;
    esac
}

cmd_stop() {
    check_engine_ready
    drop_ctr "$SERVER_CTR"; drop_ctr "$CLIENT_CTR"
    echo "==> stopped"
}

cmd_clean() {
    cmd_stop
    "$ENGINE" network rm "$NET" >/dev/null 2>&1 || true
    "$ENGINE" rmi -f "$IMAGE" >/dev/null 2>&1 || true
    echo "==> removed network and image"
}

usage() { sed -n '2,30p' "$0" | sed 's/^#\{1,2\} \{0,1\}//'; }

# ----------------------------------------------------------------- main -----

DETACH=0
STREAM=""
ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        -d|--detach) DETACH=1; shift ;;
        --stream)    STREAM=1; shift ;;
        -h|--help)   usage; exit 0 ;;
        *)           ARGS+=("$1"); shift ;;
    esac
done
set -- ${ARGS[@]+"${ARGS[@]}"}

# Interactive by default so Ctrl-C stops the container you are watching.
if [ "$DETACH" = 1 ]; then RUN_MODE=(-d --rm); else RUN_MODE=(--rm -it); fi

case "${1:-}" in
    build)  cmd_build ;;
    server) cmd_server ;;
    client) cmd_client ;;
    rpc)    shift; cmd_rpc "${1:-hello}" ;;
    demo)   shift; cmd_demo "${1:-hello}" ;;
    logs)   shift; cmd_logs "${1:-server}" ;;
    stop)   cmd_stop ;;
    clean)  cmd_clean ;;
    ""|-h|--help) usage ;;
    *) echo "run.sh: unknown command '$1'" >&2; usage >&2; exit 1 ;;
esac
