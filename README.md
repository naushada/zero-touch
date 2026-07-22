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

See [DESIGN.md](DESIGN.md) for the architecture and phase plan.

## Layout

```
inc/zerotouch/   ISmsTransport, GnmiSink, gnmi_command   (the two interface seams)
src/             gnmi command layer + transport/sink impls
daemon/          zero-touchd
test/            host tests (mocks: no modem, no gRPC)
third_party/     iot, grace-server (submodules)
```

## Build (host tests)

```sh
git submodule update --init --recursive
cmake -S . -B build -DZT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```
