# zero-touch

SMS-driven **gNMI** provisioning for field devices. An operator texts

```
IOT GNMI GET /system/config/hostname
IOT GNMI SET /system/config/hostname router-7,/interfaces/interface[name=eth0]/config/enabled true
```

the daemon authenticates the sender, runs the corresponding gNMI `Get`/`Set`
against the **device-local** gNMI server, and replies over SMS.

It is an integrator: it reuses the `smsctl` engine (from **iot**) and the
`gnmi_client` (from **grace-server**) as git submodules, both unmodified.

## Two deployment shapes, one set of seams

- **`zero-touchd` (integrated)** — rides an existing iot stack: SMS via
  cellular-client, config/users/telemetry via ds-server.
- **`zero-touchd-standalone` (ds-free)** — one self-contained daemon: opens the
  modem directly (AT) and reads config/users from files. No ds-server, no
  cellular-client — for a device that runs only a gNMI server.

Both select their backends behind the same three seams — `ISmsTransport` (SMS),
`GnmiSink` (gNMI), `IModem` (the AT modem, standalone) — so the API stays fixed
while the model varies. `modem.type` selects the vendor (`auto` detects it;
WP7702 = Sierra).

See [DESIGN.md](DESIGN.md) for the architecture and [DEPLOY.md](DEPLOY.md) for
deploying either daemon on a device.

## Architecture

Solid = request, dashed = response. The dashed-outline blocks are the seams —
the only places a concrete model is chosen.

```mermaid
flowchart TB
    subgraph EDGE["SMS edge (modem varies)"]
        direction LR
        CC["cellular-client<br/><i>ds deployment — owns the modem</i>"]
        IM{{"IModem — seam #3<br/><i>standalone only</i><br/>AtModem"}}
    end

    TX{{"ISmsTransport — seam #1<br/>DsSmsTransport | AtModemTransport"}}

    subgraph ZTD["zero-touchd"]
        BR["<b>Bridge</b><br/>allow-gate → tokenize → parse_gnmi"]
        GE["GnmiExecutor<br/>auth · RBAC · reply format"]
        SE["smsctl::Executor<br/><i>reused unmodified</i>"]
        SS[("SessionStore<br/>one login, both branches")]
    end

    GS{{"GnmiSink — seam #2<br/>LocalGnmiSink"}}
    DSK{{"smsctl::DsSink<br/><i>smsctl's own seam</i><br/>LiveSink | DirectActionSink"}}

    NG[["gNMI server<br/>127.0.0.1:50051"]]
    DS[("data-store<br/>cell.* keys + triggers")]

    CC --> TX
    IM --> TX
    TX --> BR
    BR -->|"gnmi GET/SET"| GE --> GS --> NG
    BR -->|"classic IOT cmd"| SE --> DSK
    DSK -->|"ds deployment"| DS
    DSK -->|"standalone: AT + reboot"| IM
    SS -.->|Access| GE
    SS -.-> SE

    NG -.->|GetResponse| GS -.->|GnmiResult| GE
    DS -.-> DSK -.-> SE
    GE -.->|"OK … / ERR …"| BR
    SE -.->|reply text| BR
    BR -.->|"send(to, text)"| TX
    TX -.-> EDGE

    classDef seam stroke-dasharray:4 3,stroke-width:2px
    class TX,GS,DSK,IM seam
```

The gnmi and classic branches are **siblings off the `Bridge`, not a chain** —
the data store is not in the gnmi path. On a standalone box `IModem` is shared:
the same serialised AT channel carries SMS (`AtModemTransport`) and the classic
actions (`DirectActionSink`).

### Message flow

One inbound SMS end to end, in the **standalone** wiring. The ds-backed daemon is
the same sequence with `DsSmsTransport` in place of `AtModemTransport` and
`LiveSink` writing ds keys instead of `DirectActionSink` driving AT.

```mermaid
sequenceDiagram
    autonumber
    actor U as Sender (MSISDN)
    participant M as AtModem<br/>(IModem)
    participant T as AtModemTransport<br/>(ISmsTransport)
    participant B as Bridge
    participant G as GnmiExecutor
    participant S as LocalGnmiSink<br/>(GnmiSink)
    participant N as gNMI server<br/>127.0.0.1
    participant E as smsctl::Executor
    participant D as DirectActionSink<br/>(smsctl::DsSink)

    Note over M,B: startup: bridge.start() → tx.on_message(cb) → tx.start() → modem.start()

    U-->>M: MT SMS
    M->>M: +CMTI URC → AT+CMGR → PDU decode
    M->>T: SmsFn(InboundSms{sender,text,ts})
    T->>B: on_sms(in)

    B->>B: allow(sender)
    alt not enabled / not allowlisted
        B--xU: dropped in silence (no reply)
    else allowed
        B->>B: tokenize(text) → parse_gnmi(tokens)

        alt kind != NotGnmi
            B->>G: handle(cmd, sender)
            G->>G: auth(sender) → Access
            alt Access insufficient
                G-->>B: "ERR ..."
            else GET (Viewer) / SET (Admin)
                G->>S: get(xpaths) / set(updates)
                S->>S: GET: drop denylisted xpaths (path_policy)
                S->>N: gNMI Get / Set RPC
                N-->>S: GetResponse / SetResponse + grpc-status
                S->>S: GET: mask denylisted leaves in the response
                S-->>G: GnmiResult{grpc_status, paths[]}
                G-->>B: "OK ..." (clamped to 1 SMS)
            end
        else classic IOT command
            B->>E: fallback(sender, text)
            E->>D: set/get/arm_trigger(key, value)
            D->>M: AT+CGDCONT / AT+CFUN / AT+CREG? / AT+CSQ
            M-->>D: AtResult{ok, lines}
            D-->>E: value / bool
            E-->>B: reply text ("" → drop)
        end

        opt reply non-empty
            B->>T: send(sender, reply)
            T->>M: send_sms(to, text)
            M->>M: AT+CMGS (PDU, concat)
            M-->>U: MO SMS reply
        end
    end
```

Both branches converge on one return path, and `Bridge::on_sms` applies a single
rule: **empty reply → send nothing** — the silent-drop contract that keeps the
device from being an oracle. Full walkthrough in
[DESIGN.md](DESIGN.md#architecture).

## Layout

```
inc/zerotouch/   ISmsTransport, GnmiSink, IModem + command layer   (the seams)
src/             gnmi command layer + transport/sink/modem impls
daemon/          zero-touchd (integrated) + zero-touchd-standalone
sim/             zerotouch-sim (SMS CLI) + zt-gnmi-simd (a real gNMI server)
test/            host tests (mocks: no modem, no ds, no gRPC)
schemas/         zerotouch.lua — ds key schema (integrated)
packaging/       systemd units + SysV init + env / config / users files
third_party/     iot, grace-server (submodules)
```

## Try it — the simulation

`sim/` runs the **real** command path (smsctl parser/session/executor + the
zerotouch gnmi layer) behind an in-process console transport, so each line you
type lands straight at `Bridge::on_sms`. No modem, no ds-server, no device.

Two shapes, differing only in what sits behind the `GnmiSink` seam:

```sh
./sim.sh                  # CLI only — gNMI is an in-memory tree, no sockets
./sim.sh --wire           # CLI + zt-gnmi-simd, a real gNMI server over gRPC
./sim.sh --rebuild        # force a fresh image
# native: cmake -S . -B build -DZT_BUILD_SIM=ON && ./build/zerotouch-sim
```

`--wire` is the one that exercises `LocalGnmiSink` — protobuf path codecs,
`TypedValue` encoding, RBAC via `prefix.target`, response decoding — the only
place that code runs outside a device. Podman or docker (podman preferred;
`CONTAINER_ENGINE=docker` overrides).

```
$ ./sim.sh --wire
   _____               _____                _
  |__  /___ _ __ ___  |_   _|__  _   _  ___| |__
    / // _ \ '__/ _ \   | |/ _ \| | | |/ __| '_ \
   / /|  __/ | | (_) |  | | (_) | |_| | (__| | | |
  /____\___|_|  \___/   |_|\___/ \__,_|\___|_| |_|
  gNMI backend: LocalGnmiSink → gRPC zt-gnmid:50051
> IOT GNMI GET /system/config/hostname
  ← SMS to +15551230000: ERR GNMI GET login required
> IOT LOGIN admin admin
  ← SMS to +15551230000: OK LOGIN: admin, 10 min
> IOT GNMI SET /system/config/hostname,/cellular/config/apn router-7,iot.m2m
  ← SMS to +15551230000: OK GNMI SET 2 path(s) updated
> IOT GNMI GET /system/config/hostname,/cellular/config/apn
  ← SMS to +15551230000: OK GNMI GET /system/config/hostname=router-7; /cellular/config/apn=iot.m2m
> IOT GNMI GET /system/aaa
  ← SMS to +15551230000: OK GNMI GET /system/aaa/user[name=admin]/config/password=<sensitive path denied>; …
```

`/help` lists the REPL commands (`/from`, `/enable`, `/disable`, `/allow`,
`/tree`, `/users`). Full walkthrough, including what each exchange proves and
how the server behaves, in [sim/README.md](sim/README.md).

## Build (host tests)

```sh
git submodule update --init --recursive
cmake -S . -B build -DZT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

The pure command layer + tests need only a C++17 compiler (GTest is fetched if
absent). `LocalGnmiSink` — the real gNMI backend over grace-server's
`gnmi_client` — is opt-in and needs protobuf, libevent, libevent_openssl and
nghttp2 (the device/Yocto toolchain has these):

```sh
cmake -S . -B build -DZT_BUILD_GNMI=ON
```

`DsSmsTransport` — the ds/cellular-client SMS route (impl #1 of `ISmsTransport`)
— is likewise opt-in and needs ACE + the reused `datastore_client` (and its Lua
/ OpenSSL deps), all provided by the device/Yocto toolchain:

```sh
cmake -S . -B build -DZT_BUILD_DS=ON
```

`-DZT_BUILD_DAEMON=ON` implies both `ZT_BUILD_GNMI` and `ZT_BUILD_DS`. The
**standalone** appliance (`zero-touchd-standalone`, ds-free — `AtModem` +
config/users files) builds with `-DZT_BUILD_STANDALONE=ON` (implies gNMI, needs
ACE; no ds):

```sh
cmake -S . -B build -DZT_BUILD_STANDALONE=ON
```

To cross-compile the daemon for an **aarch64 (ARMv8-A)** device, use `build.sh`
with a Yocto SDK or a cross toolchain + sysroot. The device rootfs is read-only,
so production installs bake the daemon into the image via a Yocto recipe
(`packaging/yocto/`) — see
[DEPLOY.md](DEPLOY.md#deploy-to-the-device-read-only-rootfs):

```sh
./build.sh --sdk /opt/poky/<ver>/environment-setup-cortexa53-crypto-poky-linux
```

Add `--standalone` for the ds-free appliance
(`./build.sh --standalone --sdk …` → `zero-touchd-standalone-aarch64.tar.gz`).

No cross toolchain handy? `./docker-build.sh` builds the aarch64 SysV deploy
tarball in a container (native arm64 via QEMU) and drops
`zero-touchd-aarch64-sysv.tar.gz` in the current directory. The standalone image
has its own `Dockerfile.standalone`.
