/// zerotouch-sim — SMS simulator for the zero-touch command path.
///
/// Wires the REAL command path — smsctl::tokenize/parse/Executor + the zerotouch
/// Bridge/GnmiExecutor — behind an in-process console transport, so you can
/// drive the full `IOT …` conversation from a keyboard and see the reply SMS.
/// Same engine the device runs; only the transport (console) is swapped in.
///
/// The gNMI backend is chosen at runtime through the GnmiSink seam:
///   (default)            in-memory tree — no gRPC at all, builds anywhere
///   --gnmi=HOST:PORT     the REAL LocalGnmiSink, talking gRPC to a gNMI server
///                        (run zt-gnmi-simd). Needs the ZT_BUILD_GNMI build.
/// The second mode is the one that exercises protobuf path/TypedValue codecs,
/// RBAC-via-prefix.target and response decoding — i.e. the wire. See sim/README.md.

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

#ifdef ZT_SIM_WITH_GNMI
#include "zerotouch/local_gnmi_sink.hpp"
#endif

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

#ifdef ZT_SIM_WITH_GNMI
/// Decorates a GnmiSink, silencing std::cout for the duration of the call.
///
/// grace-server's reactor chats on stdout — "Fn:~evt_io:255 dtor" every time a
/// connection is torn down, which is once per RPC — and in a REPL that lands in
/// the middle of the conversation. Only the library's own logging is dropped;
/// the RPC and its result are untouched. (gnmi_peer solves the same problem by
/// redirecting std::cout to a logfile for its whole run.)
class QuietSink : public GnmiSink {
public:
    explicit QuietSink(GnmiSink& inner) : m_inner(inner) {}

    GnmiResult get(const std::vector<std::string>& xpaths) override {
        Hush h;
        return m_inner.get(xpaths);
    }
    GnmiResult set(
        const std::vector<std::pair<std::string, std::string>>& updates) override {
        Hush h;
        return m_inner.set(updates);
    }

private:
    /// RAII: swap std::cout's buffer for a scratch one, restore on scope exit.
    struct Hush {
        std::ostringstream  swallowed;
        std::streambuf*     saved;
        Hush() : saved(std::cout.rdbuf(swallowed.rdbuf())) {}
        ~Hush() { std::cout.rdbuf(saved); }
    };

    GnmiSink& m_inner;
};
#endif

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

void banner(const std::string& from, bool enabled, const smsctl::SessionStore& s,
            const std::string& backend) {
    std::cout << "zerotouch-sim — SMS simulator (no modem, no ds-server)\n"
              << "  gNMI backend: " << backend << "\n"
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
        "  /tree               dump the gNMI store (remote: GET / over gRPC)\n"
        "  /users              list the demo users\n"
        "  /help               this help\n"
        "  /quit | /exit | quit | exit | q    leave\n"
        "examples:\n"
        "  IOT LOGIN admin admin\n"
        "  IOT GNMI GET /system/config/hostname\n"
        "  IOT GNMI GET /system/config/hostname,/system/aaa/user[name=admin]/config/password\n"
        "  IOT GNMI SET /system/config/hostname router-7\n"
        "  IOT STATUS\n";
}

void usage() {
    std::cout <<
        "zerotouch-sim — SMS simulator for the zero-touch command path\n"
        "\nUsage: zerotouch-sim [--gnmi=HOST:PORT] [--help]\n"
        "  --gnmi=HOST:PORT  drive a REAL gNMI server over gRPC via\n"
        "                    LocalGnmiSink (run zt-gnmi-simd there).\n"
        "                    Omit for the built-in in-memory gNMI tree.\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string   remote_host;
    std::uint16_t remote_port = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--gnmi=", 0) == 0) {
            const std::string hp = a.substr(7);
            const auto colon = hp.rfind(':');
            if (colon == std::string::npos) {
                std::cerr << "zerotouch-sim: --gnmi needs HOST:PORT\n";
                return 2;
            }
            remote_host = hp.substr(0, colon);
            remote_port = static_cast<std::uint16_t>(std::stoi(hp.substr(colon + 1)));
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else {
            std::cerr << "zerotouch-sim: unknown argument '" << a << "'\n";
            usage();
            return 2;
        }
    }

#ifndef ZT_SIM_WITH_GNMI
    (void)remote_port;   // only read by the LocalGnmiSink build
    if (!remote_host.empty()) {
        std::cerr << "zerotouch-sim: --gnmi needs a ZT_BUILD_GNMI=ON build "
                     "(protobuf/libevent/nghttp2).\n"
                     "  Rebuild: cmake -S . -B build -DZT_BUILD_SIM=ON "
                     "-DZT_BUILD_GNMI=ON\n"
                     "  Or run the two-service demo: ./sim.sh --wire\n";
        return 2;
    }
#endif

    // ── in-memory backends ──────────────────────────────────────────────────
    MemDsSink   ds;
    MemGnmiSink gnmi;
    // Seed a small gNMI tree, incl. a sensitive leaf to show the denylist.
    // Only used in the default (no --gnmi) mode; the remote server carries its
    // own tree, seeded from sim/gnmi-tree.conf.
    gnmi.tree["/system/config/hostname"]                        = "demo-router";
    gnmi.tree["/system/state/uptime"]                           = "12345";
    gnmi.tree["/interfaces/interface[name=eth0]/state/oper-status"] = "UP";
    gnmi.tree["/system/aaa/user[name=admin]/config/password"]   = "s3cr3t";

    // Pick the gNMI backend behind the seam. The Bridge/GnmiExecutor above
    // cannot tell the difference — which is the point of GnmiSink.
    std::string backend = "in-memory tree (no gRPC)";
    GnmiSink*   sink    = &gnmi;
#ifdef ZT_SIM_WITH_GNMI
    // Roles left at their defaults (VIEWER for Get, ADMIN for Set) and the
    // deny_tokens empty so LocalGnmiSink uses default_deny_tokens().
    LocalGnmiSink::Config lc;
    lc.host = remote_host;
    lc.port = remote_port;
    LocalGnmiSink local{std::move(lc)};
    QuietSink     quiet{local};
    if (!remote_host.empty()) {
        sink    = &quiet;
        backend = "LocalGnmiSink → gRPC " + remote_host + ":" +
                  std::to_string(remote_port);
    }
#endif

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
    GnmiExecutor gex(*sink, authfn);

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

    banner(from, enabled, sessions, backend);

    // ── REPL ────────────────────────────────────────────────────────────────
    std::string line;
    std::cout << "> " << std::flush;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF
        if (line == "quit" || line == "exit" || line == "q") break; // bare quit
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
                if (sink == &gnmi) {
                    for (const auto& [k, v] : gnmi.tree)
                        std::cout << "  " << k << " = " << v << "\n";
                } else {
                    // Remote: a GET of "/" is a subtree read of the whole tree —
                    // a real RPC, so this also proves the server is reachable.
                    const GnmiResult r = sink->get({"/"});
                    if (!r.ok) {
                        std::cout << "  (gNMI GET / failed: status "
                                  << r.grpc_status
                                  << (r.grpc_message.empty()
                                          ? "" : " " + r.grpc_message)
                                  << ")\n";
                    } else {
                        for (const auto& p : r.paths)
                            std::cout << "  " << p.xpath << " = "
                                      << (p.error.empty() ? p.value
                                                          : "<" + p.error + ">")
                                      << "\n";
                    }
                }
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
