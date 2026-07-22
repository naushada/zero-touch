# Deploying zero-touchd

`zero-touchd` runs on the device and turns authenticated SMS into gNMI `Get`/`Set`
against the **device-local** gNMI server, plus the classic smsctl command set.
It is inert until `zerotouch.enabled=true`. See [DESIGN.md](DESIGN.md) for the
architecture.

## Prerequisites on the device

- **ds-server** (`iot-ds.service`) — the daemon's entire control/telemetry surface.
- **cellular-client** — publishes the inbound SMS envelope (`sms.version`,
  `sms.last.*`) and consumes the outbound one (`sms.send.*`). No modem ⇒ the
  daemon simply stays idle.
- **A device-local gNMI server** listening on `127.0.0.1:<zerotouch.gnmi.port>`
  (default `50051`). Classic SMS commands work without it; `IOT GNMI …` returns
  `ERR GNMI …` (transport error) when it is absent.
- Group `iot` membership (granted by the unit) for the ds socket and the
  `/run/iot` trigger files the classic engine arms.

## Build & install (writable filesystem — dev host or staging)

This section is the direct `cmake --install` flow for a **writable** filesystem
(a dev box, a container, or staging a tree you later bake into an image). For the
**read-only production device**, skip to
[Deploy to the device (read-only rootfs)](#deploy-to-the-device-read-only-rootfs),
which installs via a Yocto recipe instead.

`zero-touchd` needs protobuf, libevent, libevent_openssl, nghttp2, ACE and Lua —
all present in the device toolchain. The pure command layer + host tests need
none of these.

```sh
git submodule update --init --recursive        # pulls smsctl + gnmi_client + json
cmake -S . -B build -DZT_BUILD_DAEMON=ON \
      -DZT_SYSTEMD_DIR=/lib/systemd/system      # target sysroot as needed
cmake --build build
cmake --install build --prefix /usr/local
```

`ZT_BUILD_DAEMON=ON` implies `ZT_BUILD_GNMI` + `ZT_BUILD_DS`. Install lays down:

| File | Path |
|------|------|
| `zero-touchd` binary | `/usr/local/bin/zero-touchd` |
| ds schema | `/etc/iot/ds-schemas/zerotouch.lua` |
| env file | `/etc/iot/zerotouchd.env` |
| systemd unit | `/lib/systemd/system/zero-touchd.service` |
| SysV init script (with `-DZT_INSTALL_SYSV=ON`) | `/etc/init.d/zero-touchd` |
| this doc | `/usr/local/share/doc/zero-touch/DEPLOY.md` |

### Boards without systemd (SysV / BusyBox)

Configure with `-DZT_INSTALL_SYSV=ON` to install `/etc/init.d/zero-touchd`
(LSB-headered, uses `start-stop-daemon` when present, else a plain fork; runs the
daemon in group `iot` just like the unit). On a **writable** filesystem register
and start it directly:

```sh
update-rc.d zero-touchd defaults      # or: chkconfig --add zero-touchd
/etc/init.d/zero-touchd start
/etc/init.d/zero-touchd status
```

> On a **read-only** device this registration happens at **image-build time**
> (the Yocto recipe's `update-rc.d` class creates the `rc.d` links in the
> image) — `update-rc.d` cannot run against the read-only rootfs at runtime.
> See [Deploy to the device (read-only rootfs)](#deploy-to-the-device-read-only-rootfs).

## Deploy to the device (read-only rootfs)

> **The device rootfs is read-only** — only `/tmp` and `/run` are writable. So
> `zero-touchd` is installed by building it **into the image**, never by copying
> files onto a running device: you cannot write `/usr/bin`, `/etc/init.d` or
> `/lib/systemd/system`, and `update-rc.d` / `systemctl enable` fail at runtime
> because they create symlinks under the read-only rootfs.

The daemon is designed for this: it writes **nothing** to the rootfs. Its only
runtime writes are the SysV PID file (`/run/zero-touchd.pid`) and the classic
engine's trigger files (`/run/iot/*.request`) — both on tmpfs — and its
persistent config lives in ds-server's store on the data partition.

### Production — bake into the image (Yocto recipe)

Use `packaging/yocto/zero-touchd_git.bb` (see `packaging/yocto/README.md`). Drop
it into a layer and add the package to your image:

```bitbake
IMAGE_INSTALL:append = " zero-touchd"
```

The recipe cross-compiles the daemon (deps — ACE, protobuf, libevent, nghttp2,
lua, openssl — from the sysroot) and lays the binary, `zerotouch.lua` schema,
env file and service into the rootfs. On **systemd** images it registers and
auto-enables `zero-touchd.service`; on **SysV** images it installs
`/etc/init.d/zero-touchd` and creates the `rc.d` links **at image-build time**.
Either way the daemon ships **inert** (`zerotouch.enabled=false`) until you flip
the ds key. Rebuild the image and flash / A-B OTA as usual.

### Cross-build just the binary (dev loop / tmpfs test)

`build.sh` cross-compiles `zero-touchd` and stages a tree + tarball. Use it to
feed the recipe's dev loop, or to smoke-test a binary on a running device without
reflashing:

```sh
# Yocto SDK (recommended) or a plain cross toolchain + sysroot:
./build.sh --sdk /opt/poky/<ver>/environment-setup-cortexa53-crypto-poky-linux
./build.sh --sysroot /path/to/aarch64-sysroot --prefix aarch64-linux-gnu-
```

It prints `file`'s verdict so you can confirm the binary is `ELF 64-bit … ARM
aarch64` before shipping.

**No cross toolchain? Build in a container.** `Dockerfile` + `docker-build.sh`
compile the daemon **natively as arm64** (via QEMU/binfmt), pulling deps from
Debian's arm64 packages, and export the SysV deploy tarball to the host's
current directory:

```sh
docker run --privileged --rm tonistiigi/binfmt --install arm64   # one-time
./docker-build.sh                    # → ./zero-touchd-aarch64-sysv.tar.gz
```

The tarball is rooted at the device filesystem (`usr/bin/zero-touchd`,
`etc/init.d/zero-touchd`, the ds schema, env, and the systemd unit). It is **not**
copied onto a running read-only device — extract it into your image rootfs, or
into `/run` for a tmpfs smoke test (below).

### Quick test on a running device — run from `/run` (no reflash)

`/run` is writable tmpfs, so you can drop the binary there and run it directly —
no init script, no rootfs writes, gone on reboot. This exercises the real daemon
against the device's ds + gNMI server; the `zerotouch.*` schema must already be
in the image (from the recipe) for `ds-cli set` to validate.

```sh
scp dist/aarch64/usr/bin/zero-touchd root@<device>:/run/
ssh root@<device> 'chmod +x /run/zero-touchd && /run/zero-touchd &'
```

### Point it at the on-device gNMI server

`zero-touchd` is a gNMI **client** of the server this device already runs. Set
the port to match that server's loopback listen port:

```sh
ds-cli set zerotouch.gnmi.port 50051                # match the local gNMI server
```

The target host is fixed at `127.0.0.1` and is never taken from the SMS. gNMI
`GET`/`SET` only work while that server is listening; classic SMS commands work
regardless. For start ordering, add the gNMI server's unit / init name to the
service's dependency line (systemd `After=`, or the init script's
`# Required-Start:`, which already lists `iot-ds`) **in the source before you
build the image** — it cannot be edited on the read-only device.

### Enable and start

```sh
ds-cli set zerotouch.enabled true
ds-cli set zerotouch.allowed.numbers '"+919096383701"'

# systemd image (already enabled at image build → starts at boot):
systemctl restart zero-touchd.service ; systemctl status zero-touchd.service
# SysV image:
/etc/init.d/zero-touchd restart ; /etc/init.d/zero-touchd status
```

`zerotouch.state` flips to `listening`. Text `IOT GNMI GET /system/config/hostname`
from an allowlisted phone to confirm the round trip.

## Test the command grammar offline first

Before touching a device, drive the exact same engine from a keyboard with
`zerotouch-sim` (host build, no modem/ds/gRPC) — see the README. It's the
fastest way to learn the `IOT GNMI …` grammar and confirm the sensitive-path
denial and allowlist/enabled gating behave as you expect.

## Enable

```sh
systemctl enable --now zero-touchd.service     # starts inert (ships disabled)

# Turn it on and gate it to known senders (ds keys hot-apply — no restart):
ds-cli set zerotouch.enabled true
ds-cli set zerotouch.allowed.numbers '"+919096383701,+4915112345678"'
ds-cli set zerotouch.gnmi.port 50051
```

`zerotouch.state` flips `disabled` → `listening`. Confirm with
`journalctl -u zero-touchd -f`.

## Use (from an allowlisted phone → the device's SIM number)

```
IOT LOGIN alice s3cr3t
IOT GNMI GET /system/config/hostname
IOT GNMI GET /interfaces/interface[name=eth0]/state/oper-status,/system/state/uptime
IOT GNMI SET /system/config/hostname router-7
IOT GNMI SET /a,/b 1,2
IOT LOGOUT
```

- `IOT LOGIN` first — the same users/passwords as the device UI (`auth.users.*`).
  One login authorises both gnmi and classic commands.
- `GET` needs a Viewer (or Admin) session; `SET` needs an **Admin** session.
- Replies are one SMS: `OK GNMI GET /p=val; …`, `OK GNMI SET n path(s) updated`,
  or `ERR GNMI …`.

## Security notes

- The login password and any gNMI **value** cross the carrier in **plaintext**.
  Use a dedicated Admin account for SMS, not the shared `admin`.
- gNMI **GET** replies are plaintext too: sensitive paths (`password`, `psk`,
  `secret`, `pre-shared`, `api-key`, …) are denied at the daemon and never
  returned — the reply row shows `<sensitive path denied>` instead of the value.
- The gNMI target is pinned to `127.0.0.1`; it is never taken from the SMS.
- SMS sender IDs are spoofable, so the password/session is the gate — not the
  MSISDN. Non-allowlisted or non-E.164 senders are dropped in silence.
- Command **arguments are never logged or written to ds** — only the keyword and
  `ok`/`err` outcome (`zerotouch.last.*`).

## Disable / roll back

```sh
ds-cli set zerotouch.enabled false      # instant: inbound SMS ignored
systemctl disable --now zero-touchd.service
```
