SUMMARY = "zero-touchd — device control + gNMI provisioning over SMS"
DESCRIPTION = "Turns authenticated MT-SMS (IOT GNMI GET/SET + the classic smsctl \
command set) into gNMI Get/Set against the device-local gNMI server and replies \
over SMS. Reuses the iot smsctl engine + grace-server gnmi_client. Baked into a \
read-only image; the daemon writes nothing to the rootfs (PID → /run, trigger \
files → /run/iot, persistent config → ds-server's store)."
HOMEPAGE = "https://github.com/naushada/zero-touch"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0000000000000000000000000000000000000000"
SECTION = "net"

# ── Fetch ────────────────────────────────────────────────────────────────────
# gitsm:// pulls the two submodules (iot → smsctl + datastore_client + json;
# grace-server → gnmi_client). If your CI mirrors are picky about iot's nested
# json submodule, pin submodule SRCREVs or pre-populate DL_DIR; see
# packaging/yocto/README.md.
ZT_BRANCH ?= "main"
SRC_URI = "gitsm://github.com/naushada/zero-touch.git;protocol=https;branch=${ZT_BRANCH}"
SRCREV  = "${AUTOREV}"
PV = "1.0+git${SRCPV}"

S = "${WORKDIR}/git"
B = "${WORKDIR}/build"

# ── Dependencies (all from the target sysroot) ───────────────────────────────
DEPENDS = "ace-tao openssl protobuf protobuf-native libevent nghttp2 lua zlib"
RDEPENDS:${PN} = "ace-tao openssl"

inherit cmake pkgconfig
# systemd images register the unit; SysV images get the init script + rc links,
# both created at IMAGE-BUILD time (the rootfs is read-only at runtime).
inherit ${@bb.utils.contains('DISTRO_FEATURES', 'systemd', 'systemd', 'update-rc.d', d)}

# ── Build ────────────────────────────────────────────────────────────────────
# ACE_ROOT points at the sysroot's /usr so the daemon's -I/-L are absolute and
# never touch the host. Install prefix is /usr (Yocto), not /usr/local.
EXTRA_OECMAKE = "\
    -DCMAKE_BUILD_TYPE=Release \
    -DZT_BUILD_DAEMON=ON \
    -DZT_BUILD_TESTS=OFF \
    -DZT_BUILD_SIM=OFF \
    -DZT_INSTALL_SYSV=${@bb.utils.contains('DISTRO_FEATURES', 'systemd', 'OFF', 'ON', d)} \
    -DZT_SYSTEMD_DIR=${systemd_system_unitdir} \
    -DACE_ROOT=${RECIPE_SYSROOT}${prefix} \
    -DCMAKE_INSTALL_PREFIX=${prefix} \
"

do_install:append() {
    # The shipped unit / init script default to /usr/local/bin (host install);
    # in the image the binary is ${bindir}. Rewrite the ExecStart / DAEMON path.
    if [ -f ${D}${systemd_system_unitdir}/zero-touchd.service ]; then
        sed -i 's|/usr/local/bin/zero-touchd|${bindir}/zero-touchd|' \
            ${D}${systemd_system_unitdir}/zero-touchd.service
    fi
    if [ -f ${D}${sysconfdir}/init.d/zero-touchd ]; then
        sed -i 's|/usr/local/bin/zero-touchd|${bindir}/zero-touchd|' \
            ${D}${sysconfdir}/init.d/zero-touchd
    fi
}

# ── Packaging ────────────────────────────────────────────────────────────────
FILES:${PN} += "\
    ${bindir}/zero-touchd \
    ${sysconfdir}/iot/ds-schemas/zerotouch.lua \
    ${sysconfdir}/iot/zerotouchd.env \
    ${sysconfdir}/init.d/zero-touchd \
    ${systemd_system_unitdir}/zero-touchd.service \
"

# systemd: register + auto-enable the unit at image build. The daemon itself is
# INERT until zerotouch.enabled=true (ds key), so auto-enabling the unit is safe.
SYSTEMD_SERVICE:${PN} = "zero-touchd.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

# SysV: register the init script + rc.d links at image build (update-rc.d cannot
# run on the read-only device at runtime).
INITSCRIPT_NAME = "zero-touchd"
INITSCRIPT_PARAMS = "defaults 90 10"
