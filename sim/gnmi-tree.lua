-- Seed data for zt-gnmi-simd — the simulated device's gNMI config tree.
--
-- Edit and restart the server; no rebuild. Values keep their Lua type: strings
-- stay strings, numbers and booleans are rendered as they read here ("1500",
-- "true"), which is what the gNMI TypedValue carries.
--
-- A GET of a non-leaf path returns the whole subtree, so
--   IOT GNMI GET /system/config
-- returns every /system/config/* leaf below.

return {
  tree = {
    -- ── system ──────────────────────────────────────────────────────────
    ["/system/config/hostname"]        = "demo-router",
    ["/system/config/domain-name"]     = "field.example.net",
    ["/system/config/login-banner"]    = "zero-touch demo device",
    ["/system/state/boot-time"]        = 1755400000,
    ["/system/state/software-version"] = "1.4.2",

    -- ── interfaces ──────────────────────────────────────────────────────
    ["/interfaces/interface[name=eth0]/config/enabled"]      = true,
    ["/interfaces/interface[name=eth0]/config/description"]  = "uplink",
    ["/interfaces/interface[name=eth0]/config/mtu"]          = 1500,
    ["/interfaces/interface[name=eth0]/state/oper-status"]   = "UP",
    ["/interfaces/interface[name=wwan0]/config/enabled"]     = true,
    ["/interfaces/interface[name=wwan0]/config/description"] = "lte",
    ["/interfaces/interface[name=wwan0]/state/oper-status"]  = "DOWN",

    -- ── cellular ────────────────────────────────────────────────────────
    ["/cellular/config/apn"]         = "internet",
    ["/cellular/state/registration"] = "home",
    ["/cellular/state/signal-dbm"]   = -79,

    -- ── credentials: present ONLY to demonstrate the denylist ───────────
    -- LocalGnmiSink masks these on both legs of a GET: a request naming one
    -- never leaves the device, and a subtree GET (/system/aaa, /) has them
    -- stripped from the response. Watch the server log stay silent on the
    -- first case, and show the leaves — masked in the SMS — on the second.
    ["/system/aaa/user[name=admin]/config/password"]  = "s3cr3t",
    ["/system/aaa/user[name=viewer]/config/password"] = "v13w",
    ["/wifi/config/pre-shared-key"]                   = "hunter2",
  },
}
