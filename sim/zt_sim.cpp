/// zerotouch-sim — offline SMS simulator (no modem, no ds-server, no gRPC).
///
/// Wires the REAL command path — smsctl::tokenize/parse/Executor + the zerotouch
/// Bridge/GnmiExecutor — against in-memory ds and gNMI stores, so you can drive
/// the full `IOT …` conversation from a keyboard and see the reply SMS. Same
/// engine the device runs; only the transport (console) and the two backends
/// (in-memory) are swapped in via the interface seams. See DESIGN.md.

#include <cstdint>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "smsctl/executor.hpp"
#include "smsctl/parser.hpp"
#include "smsctl/session.hpp"

#include "zerotouch/bridge.hpp"
#include "zerotouch/gnmi_executor.hpp"
#include "zerotouch/gnmi_sink.hpp"
#include "zerotouch/path_policy.hpp"
#include "zerotouch/sms_transport.hpp"

using namespace zerotouch;

namespace {

std::uint64_t now_s() { return static_cast<std::uint64_t>(std::time(nullptr)); }

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else if (c != ' ' && c != '\t') cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/// In-memory stand-in for the ds the classic smsctl::Executor writes/reads.
class MemDsSink : public smsctl::DsSink {
public:
    bool set(const std::string& key, const std::string& value) override {
        kv[key] = value;
        return true;
    }
    std::optional<std::string> get(const std::string& key) override {
        auto it = kv.find(key);
        if (it == kv.end()) return std::nullopt;
        return it->second;
    }
    bool arm_trigger(const std::string& path, const std::string&) override {
        std::cout << "  · [trigger armed] " << path << "\n";
        return true;
    }
    std::uint64_t now_ms() override { return now_s() * 1000ULL; }

    std::map<std::string, std::string> kv;
};

/// In-memory gNMI server: a flat path→value tree. Mirrors LocalGnmiSink's
/// contract — the sensitive-path denylist blocks GET (never SET), and ok stays
/// true so per-path rows (denied / not-found) render in the OK reply.
class MemGnmiSink : public GnmiSink {
public:
    std::map<std::string, std::string> tree;

    GnmiResult get(const std::vector<std::string>& xpaths) override {
        GnmiResult r;
        r.ok = true;
        r.grpc_status = 0;
        for (const auto& xp : xpaths) {
            if (is_sensitive_path(xp)) {
                r.paths.push_back({xp, "", "sensitive path denied"});
                continue;
            }
            auto it = tree.find(xp);
            if (it == tree.end()) r.paths.push_back({xp, "", "not found"});
            else                  r.paths.push_back({xp, it->second, ""});
        }
        return r;
    }
    GnmiResult set(
        const std::vector<std::pair<std::string, std::string>>& updates) override {
        GnmiResult r;
        r.ok = true;
        r.grpc_status = 0;
        for (const auto& [xp, val] : updates) {
            tree[xp] = val;
            r.paths.push_back({xp, "", ""});
        }
        return r;
    }
};

/// Console transport: prints reply SMS, injects inbound from the REPL.
class ConsoleTransport : public ISmsTransport {
public:
    void on_message(MessageFn cb) override { m_cb = std::move(cb); }
    bool send(const std::string& to, const std::string& text) override {
        std::cout << "  ← SMS to " << to << ": " << text << "\n";
        ++sent;
        return true;
    }
    void start() override {}
    void inject(const InboundSms& in) { if (m_cb) m_cb(in); }
    int sent = 0;

private:
    MessageFn m_cb;
};

void banner(const std::string& from, bool enabled, const smsctl::SessionStore& s) {
    std::cout << "zerotouch-sim — offline SMS simulator (no modem/ds/gRPC)\n"
              << "  from=" << from << "  enabled=" << (enabled ? "yes" : "no")
              << "  allowlist=" << s.config().allowed_numbers.size() << " number(s)\n"
              << "  demo users: admin/admin (Admin), viewer/viewer (Viewer)\n"
              << "  type an SMS body, or /help.  Try: IOT LOGIN admin admin\n";
}

void help() {
    std::cout <<
        "commands:\n"
        "  <text>              send <text> as an SMS from the current sender\n"
        "  /from <number>      set the sender MSISDN (default +15551230000)\n"
        "  /enable | /disable  toggle zerotouch.enabled (disabled => silent drop)\n"
        "  /allow <csv>        set the allowlist (empty => any sender may login)\n"
        "  /tree               dump the in-memory gNMI store\n"
        "  /users              list the demo users\n"
        "  /help               this help\n"
        "  /quit | /exit       leave\n"
        "examples:\n"
        "  IOT LOGIN admin admin\n"
        "  IOT GNMI GET /system/config/hostname\n"
        "  IOT GNMI GET /system/config/hostname,/system/aaa/user[name=admin]/config/password\n"
        "  IOT GNMI SET /system/config/hostname router-7\n"
        "  IOT STATUS\n";
}

} // namespace

int main() {
    // ── in-memory backends ──────────────────────────────────────────────────
    MemDsSink   ds;
    MemGnmiSink gnmi;
    // Seed a small gNMI tree, incl. a sensitive leaf to show the denylist.
    gnmi.tree["/system/config/hostname"]                        = "demo-router";
    gnmi.tree["/system/state/uptime"]                           = "12345";
    gnmi.tree["/interfaces/interface[name=eth0]/state/oper-status"] = "UP";
    gnmi.tree["/system/aaa/user[name=admin]/config/password"]   = "s3cr3t";

    // ── demo users (same hashing as the device UI / smsctl login) ───────────
    std::map<std::string, smsctl::Account> users = {
        {"admin",  {"admin",  smsctl::sha256_hex("admin"),  "Admin"}},
        {"viewer", {"viewer", smsctl::sha256_hex("viewer"), "Viewer"}},
    };
    auto lookup = [&](const std::string& id, smsctl::Account& out) {
        auto it = users.find(id);
        if (it == users.end()) return false;
        out = it->second;
        return true;
    };

    smsctl::SessionStore sessions;   // real: login / lockout / TTL
    bool          enabled  = true;
    std::uint64_t handled  = 0;
    std::string   from     = "+15551230000";

    // ── the same wiring zero-touchd builds ──────────────────────────────────
    auto authfn = [&](const std::string& sender) -> Access {
        const smsctl::Account* a = sessions.session(sender, now_s());
        if (!a) return Access::None;
        return a->access == "Admin" ? Access::Admin : Access::Viewer;
    };
    GnmiExecutor gex(gnmi, authfn);

    auto fallback = [&](const std::string& sender, const std::string& text) {
        const smsctl::Command cmd = smsctl::parse(text);
        if (cmd.kind == smsctl::Kind::NotACommand) return std::string();
        const std::uint64_t now  = now_s();
        const std::uint64_t seed = now * 2654435761ULL + (++handled);
        smsctl::Executor ex(ds, sessions, lookup);
        return ex.handle(cmd, sender, now, seed);
    };
    auto allow = [&](const std::string& sender) {
        return enabled && sessions.sender_allowed(sender);
    };

    ConsoleTransport tx;
    Bridge bridge(tx, gex, smsctl::tokenize, fallback, allow);
    bridge.start();

    banner(from, enabled, sessions);

    // ── REPL ────────────────────────────────────────────────────────────────
    std::string line;
    std::cout << "> " << std::flush;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line[0] == '/') {
            std::istringstream ss(line);
            std::string cmd; ss >> cmd;
            std::string rest; std::getline(ss, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

            if (cmd == "/quit" || cmd == "/exit") break;
            else if (cmd == "/help") help();
            else if (cmd == "/from" && !rest.empty()) { from = rest;
                std::cout << "  sender = " << from << "\n"; }
            else if (cmd == "/enable")  { enabled = true;  std::cout << "  enabled\n"; }
            else if (cmd == "/disable") { enabled = false; std::cout << "  disabled\n"; }
            else if (cmd == "/allow") {
                smsctl::Config c = sessions.config();
                c.allowed_numbers = split_csv(rest);
                sessions.set_config(std::move(c));
                std::cout << "  allowlist = " << sessions.config().allowed_numbers.size()
                          << " number(s)\n";
            }
            else if (cmd == "/tree") {
                for (const auto& [k, v] : gnmi.tree) std::cout << "  " << k << " = " << v << "\n";
            }
            else if (cmd == "/users") {
                for (const auto& [id, a] : users)
                    std::cout << "  " << id << " (" << a.access << ")\n";
            }
            else std::cout << "  unknown command (try /help)\n";
        } else if (!line.empty()) {
            const int before = tx.sent;
            std::cout << "  → SMS from " << from << ": " << line << "\n";
            tx.inject({from, line, std::to_string(now_s())});
            if (tx.sent == before)
                std::cout << "  (no reply — dropped: disabled, not allowlisted, "
                             "or not an IOT command)\n";
        }
        std::cout << "> " << std::flush;
    }
    std::cout << "bye\n";
    return 0;
}
