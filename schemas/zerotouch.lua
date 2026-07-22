-- zerotouch.* schema for zero-touchd — device control + gNMI provisioning over SMS.
--
-- The daemon consumes the MT-SMS envelope cellular-client publishes
-- (sms.version + sms.last.*), authenticates the sender against the device's own
-- users (auth.users.admin.password.hash / auth.users.accounts — the same hashes
-- the device-ui login uses), and either runs a gNMI Get/Set against the
-- DEVICE-LOCAL gNMI server (127.0.0.1:<zerotouch.gnmi.port>) or a classic smsctl
-- command, then answers with a single MO SMS via sms.send.*.
--
-- Command grammar (text these to the device's SIM number; one IOT LOGIN first):
--   IOT GNMI GET <xpath[,xpath...]>
--   IOT GNMI SET <xpath[,xpath...]> <value[,value...]>   -- Admin session
--   plus the classic smsctl set: IOT LOGIN/LOGOUT/STATUS/REBOOT/FACTORY-RESET/
--   APN/RADIO RESTART/WIFI (see the smsctl schema).
--
-- SHIPS DISABLED (zerotouch.enabled=false). Enable per device, because the login
-- password necessarily crosses the carrier in plaintext — prefer a dedicated
-- Admin account for SMS over the shared `admin` one. A gNMI GET reply also
-- crosses the carrier in plaintext, so sensitive paths (password/psk/secret/…)
-- are denied at the daemon and never returned.
--
-- Read keys (operator -> daemon):
--   zerotouch.enabled           - master switch; the daemon ignores all inbound
--                                 SMS while false (default false)
--   zerotouch.gnmi.port         - device-local gNMI server port the daemon
--                                 targets on 127.0.0.1 (default 50051). The
--                                 target host is fixed; it is never taken from
--                                 the SMS.
--   zerotouch.allowed.numbers   - CSV of E.164 senders permitted to issue
--                                 commands. Empty = any sender may attempt LOGIN
--                                 (the password is still required). A non-allowed
--                                 sender is dropped SILENTLY - no reply, so the
--                                 device is not an oracle and carrier spam costs
--                                 nothing. Matching is on the last 9 digits.
--   zerotouch.session.ttl.sec   - login session lifetime (default 600)
--   zerotouch.lockout.failures  - failed logins per sender before lockout (5)
--   zerotouch.lockout.sec       - lockout window (default 900)
--
-- Write keys (daemon -> operator / device-ui):
--   zerotouch.state       - "disabled" | "listening"
--   zerotouch.last.sender - sender of the last command
--   zerotouch.last.cmd    - the last command's KEYWORD ONLY. Arguments are never
--                           stored: a LOGIN password / WiFi PSK / gNMI value must
--                           never reach ds or the journal.
--   zerotouch.last.result - "ok" | "err"
--   zerotouch.last.ts     - epoch seconds of the last command
--   zerotouch.sessions    - number of live sessions
--   zerotouch.version     - bump-on-change counter for the device-ui long-poll
--
-- Install at /etc/iot/ds-schemas/zerotouch.lua (ds-server auto-loads).

local function viewer_str() return { access = "Viewer", type = "string", default = "" } end

return {
  namespace = "zerotouch",
  keys = {
    -- ── config (operator-set) ──────────────────────────────────────────
    ["zerotouch.enabled"]          = { access = "Admin", type = "boolean", default = false },
    ["zerotouch.gnmi.port"]        = { access = "Admin", type = "integer", default = 50051,
                                       min = 1,  max = 65535 },
    ["zerotouch.allowed.numbers"]  = { access = "Admin", type = "string",  default = "" },
    ["zerotouch.session.ttl.sec"]  = { access = "Admin", type = "integer", default = 600,
                                       min = 60, max = 86400 },
    ["zerotouch.lockout.failures"] = { access = "Admin", type = "integer", default = 5,
                                       min = 1,  max = 20 },
    ["zerotouch.lockout.sec"]      = { access = "Admin", type = "integer", default = 900,
                                       min = 60, max = 86400 },

    -- ── status (daemon-published) ──────────────────────────────────────
    ["zerotouch.state"]       = viewer_str(),
    ["zerotouch.last.sender"] = viewer_str(),
    ["zerotouch.last.cmd"]    = viewer_str(),   -- keyword only, NEVER arguments
    ["zerotouch.last.result"] = viewer_str(),
    ["zerotouch.last.ts"]     = viewer_str(),
    ["zerotouch.sessions"]    = viewer_str(),
    ["zerotouch.version"]     = viewer_str(),
  },
}
