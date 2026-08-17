# `sim/` — the zero-touch simulation

Drive the whole `IOT …` conversation from a keyboard, with no modem, no
ds-server and no device.

Both shapes run the **real** command path — `smsctl::tokenize` → `parse_gnmi` →
`SessionStore` auth → `GnmiExecutor` / `smsctl::Executor` → reply formatting.
That is the same code `zero-touchd` runs in the field. Only the SMS transport is
swapped for a console.

| | gNMI backend | Exercises |
|---|---|---|
| `./sim.sh` | in-process `MemGnmiSink` | the grammar, sessions, RBAC, reply format |
| `./sim.sh --wire` | **`LocalGnmiSink` over real gRPC** | all of the above **plus the wire** |

The second mode is the one worth demoing: it is the only place `LocalGnmiSink`
— protobuf path codecs, `TypedValue` encoding, RBAC via `prefix.target`,
`GetResponse`/`SetResponse` decoding — actually runs outside a device.

## Contents

```
zt_sim.cpp        zerotouch-sim — the SMS CLI (REPL)
zt_gnmi_simd.cpp  zt-gnmi-simd  — the simulated device's gNMI server
gnmi_tree.hpp     the server's config tree + lookup rules (pure, host-tested)
gnmi-tree.lua     seed data for that tree — edit freely, no rebuild
compose.yml       the two-service wiring
```

The seed is Lua (`return { tree = { ["/xpath"] = value } }`) and is read through
`lua_file` — project rules 2 and 3: configuration is Lua, and Lua I/O goes
through `lua_engine`, never `std::ifstream`. Values keep their Lua type, so
`mtu = 1500` and `enabled = true` render as `"1500"` and `"true"`.

## Run it

```sh
./sim.sh --wire          # builds the image if needed, then starts both
```

Podman or docker — podman wins when both are installed; `CONTAINER_ENGINE=docker`
forces the other. In a second terminal, watch the device side:

```sh
podman logs -f zt-gnmid
```

## A demo that lands

```
> IOT GNMI GET /system/config/hostname
  ← SMS to +15551230000: ERR GNMI GET login required
```
The MSISDN is not the credential — sender IDs are spoofable, so a session is
required before anything runs.

```
> IOT LOGIN viewer viewer
  ← SMS to +15551230000: OK LOGIN: viewer, 10 min
> IOT GNMI GET /system/config/hostname
  ← SMS to +15551230000: OK GNMI GET /system/config/hostname=demo-router
> IOT GNMI SET /system/config/hostname nope
  ← SMS to +15551230000: ERR GNMI SET admin login required
```
Read needs Viewer, write needs Admin — and the refusal happens on the device
before any RPC is built. The server log stays silent.

```
> IOT LOGIN admin admin
> IOT GNMI SET /system/config/hostname,/cellular/config/apn router-7,iot.m2m
  ← SMS to +15551230000: OK GNMI SET 2 path(s) updated
> IOT GNMI GET /system/config/hostname,/cellular/config/apn
  ← SMS to +15551230000: OK GNMI GET /system/config/hostname=router-7; /cellular/config/apn=iot.m2m
```
One SMS, two paths, one `SetRequest`, and it reads back — the write really
crossed the socket. `podman logs zt-gnmid` shows the matching `[Set] UPDATE`
lines.

```
> IOT GNMI GET /system/aaa
  ← SMS to +15551230000: OK GNMI GET /system/aaa/user[name=admin]/config/password=<sensitive path denied>; …
```
The denylist runs on **both** legs. `/system/aaa` is not itself a secret-looking
path, so the request goes out — but every leaf coming back is re-checked, and
the passwords are masked before they could reach a plaintext SMS.

```
> IOT GNMI GET /nope/missing
  ← SMS to +15551230000: ERR GNMI GET not found
```
The server closes the RPC with gRPC status 5 and no `grpc-message` (that trailer
is optional and grace-server never sends it); the reply layer names the code.

REPL helpers: `/from <num>`, `/enable`, `/disable`, `/allow <csv>`, `/tree`,
`/users`, `/help`. In `--wire` mode `/tree` is a real `GET /` — a subtree read of
the whole device.

## The server

`zt-gnmi-simd` serves gNMI `Get`/`Set` over plaintext gRPC on `:50051`, backed by
the tree in `gnmi-tree.lua`.

```
zt-gnmi-simd [--listen=HOST:PORT] [--seed=PATH] [--quiet]
```

It exists because grace-server's own handlers are **stubs** — its
`/gnmi.gNMI/Get` returns empty `Notification`s and its `/gnmi.gNMI/Set` echoes
without storing (`third_party/grace-server/hackthon/app/src/client_app.cpp`), so
a SET could never be read back by a GET. What *is* reused verbatim: `grpc_session`
(HTTP/2 + gRPC framing), `evt_io`/`run_evt_loop`, `gnmi_util` and the gnmi
protos. Only the two handlers are ours, and they keep grace-server's conventions
— role in `prefix.target`, `Set` requires `ADMIN`.

Behaviour worth knowing:

- **Subtree reads.** A Get on a non-leaf path returns every leaf beneath it;
  `/` returns the whole tree. A Get matching nothing is `NOT_FOUND` (5).
- **Two RBAC gates.** zerotouch requires an Admin *session* for `GNMI SET`; the
  server independently requires `prefix.target == "ADMIN"` and answers
  `PERMISSION_DENIED` (7) otherwise. The demo normally only shows the first,
  because `LocalGnmiSink` sends the ADMIN target.
- **Memory only.** A SET survives until the container stops; restart re-seeds
  from the file. The mount is `:ro` — the server never writes it back.
- **No TLS, no YANG validation, no `Subscribe`.** It is a simulator.

## Native build (no container)

```sh
cmake -S . -B build -DZT_BUILD_SIM=ON              # CLI only
./build/zerotouch-sim
```

Add `-DZT_BUILD_GNMI=ON` for `zt-gnmi-simd` and the `--gnmi=HOST:PORT` flag; that
needs protobuf, libevent, libevent_openssl, nghttp2 and Lua. Without it the CLI
still builds everywhere and `--gnmi` reports how to enable it.

The lookup rules in `gnmi_tree.hpp` are pure and covered by the default host
suite (`test/gnmi_tree_test.cpp`) — no protobuf needed to test them.
