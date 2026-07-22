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

## Build (device / Yocto toolchain)

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
daemon in group `iot` just like the unit). Then:

```sh
update-rc.d zero-touchd defaults      # or: chkconfig --add zero-touchd
/etc/init.d/zero-touchd start
/etc/init.d/zero-touchd status
```

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
