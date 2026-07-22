# Yocto recipe — baking zero-touchd into a read-only image

On the target the rootfs is **read-only** (only `/tmp` and `/run` are writable),
so software is installed by building it **into the image**, never by copying onto
a running device. `zero-touchd_git.bb` compiles the daemon from source (reusing
the iot `smsctl`/`datastore_client` and grace-server `gnmi_client` submodules)
and lays the binary, ds schema, env file and service down in the rootfs.

The daemon is read-only-rootfs-clean: it writes only to `/run` (SysV PID) and
`/run/iot/*.request` (classic-engine triggers on the tmpfs owned by iot's
`iot.conf`), and its persistent config is ds-server's store on the data
partition — nothing lands on the rootfs.

## Add it to a build

Drop the recipe into a layer (e.g. `meta-iot/recipes-iot/zero-touch/`) and add
the package to your image:

```bitbake
# in your image .bb / local.conf
IMAGE_INSTALL:append = " zero-touchd"
```

- **systemd images** (the iot reference): the recipe registers
  `zero-touchd.service` and auto-enables it. The daemon still ships **inert**
  (`zerotouch.enabled=false`) until you flip the ds key.
- **SysV images**: the recipe installs `/etc/init.d/zero-touchd` and creates the
  `rc.d` links at image-build time via `update-rc.d` (which cannot run on the
  read-only device at runtime).

Then rebuild and deploy the image (flash or A/B RAUC OTA) as usual.

## Notes / caveats

- `LIC_FILES_CHKSUM` has a placeholder md5 — set it to the real `LICENSE`
  checksum before merging into a product layer.
- `DEPENDS` expects `ace-tao`, `protobuf`, `libevent`, `nghttp2`, `lua`,
  `openssl`, `zlib` recipes in your layers (the iot image already has them).
- `gitsm://` fetches the submodules recursively. If your mirror trips over iot's
  nested `json` submodule, pin submodule `SRCREV`s or pre-populate `DL_DIR`; you
  can also switch to `devtool modify` with a locally-checked-out
  `git submodule update --init --recursive` tree.
- `ACE_ROOT=${RECIPE_SYSROOT}${prefix}` keeps the daemon's `-I/-L` inside the
  sysroot (no host contamination); `do_install:append` rewrites the unit /
  init-script `ExecStart` from the host default `/usr/local/bin` to `${bindir}`.

This recipe is a validated-by-construction template; run it through your actual
BSP build to confirm the deps/fetch before shipping.
