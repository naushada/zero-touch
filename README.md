# zero-touch

SMS-driven **gNMI** provisioning for field devices. An operator texts

```
IOT GNMI GET /system/config/hostname
IOT GNMI SET /system/config/hostname router-7,/interfaces/interface[name=eth0]/config/enabled true
```

and `zero-touchd` authenticates the sender, runs the corresponding gNMI
`Get`/`Set` against the **device-local** gNMI server, and replies over SMS.

It is an integrator: it reuses the `smsctl` engine (from **iot**) and the
`gnmi_client` (from **grace-server**) as git submodules, both unmodified.

See [DESIGN.md](DESIGN.md) for the architecture and [DEPLOY.md](DEPLOY.md) for
deploying `zero-touchd` on a device.

## Layout

```
inc/zerotouch/   ISmsTransport, GnmiSink, gnmi_command   (the two interface seams)
src/             gnmi command layer + transport/sink impls
daemon/          zero-touchd
sim/             zerotouch-sim — offline SMS simulator (no modem/ds/gRPC)
test/            host tests (mocks: no modem, no gRPC)
schemas/         zerotouch.lua — ds key schema + defaults
packaging/       systemd unit + SysV init script + env file
third_party/     iot, grace-server (submodules)
```

## Try it offline — `zerotouch-sim`

`zerotouch-sim` runs the **real** command path (smsctl parser/session/executor +
the zerotouch bridge/gnmi layer) against in-memory ds and gNMI stores, so you can
drive the whole `IOT …` SMS conversation from a keyboard — no modem, no
ds-server, no gRPC. It builds by default (needs the `smsctl` engine, so
`git submodule update --init --recursive` first).

```
$ ./build/zerotouch-sim
> IOT LOGIN admin admin
  ← SMS to +15551230000: OK LOGIN: admin, 10 min
> IOT GNMI GET /system/config/hostname,/system/aaa/user[name=admin]/config/password
  ← SMS to +15551230000: OK GNMI GET /system/config/hostname=demo-router; /system/aaa/user[name=admin]/config/password=<sensitive path denied>
> IOT GNMI SET /system/config/hostname router-7
  ← SMS to +15551230000: OK GNMI SET 1 path(s) updated
```

`/help` lists the REPL commands (`/from`, `/enable`, `/disable`, `/allow`,
`/tree`, `/users`).

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

`-DZT_BUILD_DAEMON=ON` implies both `ZT_BUILD_GNMI` and `ZT_BUILD_DS`.
