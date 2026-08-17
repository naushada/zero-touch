/// zt-gnmi-simd — a real gNMI server for the zero-touch demo.
///
/// Speaks gNMI Get/Set over gRPC/HTTP2 on a configurable IP:port, backed by an
/// in-memory config tree seeded from a file. It exists because grace-server's
/// own server handlers are stubs — its `/gnmi.gNMI/Get` returns empty
/// Notifications and its `/gnmi.gNMI/Set` echoes without storing (see
/// third_party/grace-server/hackthon/app/src/client_app.cpp) — so a SET could
/// never be read back by a GET and the demo would show nothing.
///
/// What IS reused, verbatim: grace-server's `grpc_session` (HTTP/2 + gRPC
/// framing), `evt_io`/`run_evt_loop` (the libevent reactor), `gnmi_util` (path
/// and TypedValue codecs) and the gnmi protos. Only the two handlers are ours,
/// and they keep grace-server's conventions — the caller's role rides in
/// `prefix.target`, and Set demands "ADMIN".
///
/// This is a SIMULATOR: no YANG schema validation, no persistence, no TLS. It
/// stands in for the device-local gNMI server that zero-touchd talks to.
/// See sim/README.md.

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

#include "framework.hpp"
#include "gnmi_util.hpp"
#include "grpc_session.hpp"
#include "lua_engine.hpp"

#include "gnmi/gnmi.pb.h"

#include "gnmi_tree.hpp"

using zerotouch::sim::GnmiTree;

namespace {

// ── process-wide demo state ─────────────────────────────────────────────────
// One tree shared by every connection: a SET on one client is visible to the
// next GET on another, which is the whole point of running a real server.
GnmiTree g_tree;
bool     g_verbose = true;

std::int64_t now_ns() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

/// Timestamped server-side log line — this is the "server half" of the demo,
/// so it is deliberately chatty about role, path count and outcome.
void log(const std::string& line) {
    if (!g_verbose) return;
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);
    std::cout << "[" << stamp << "] " << line << "\n" << std::flush;
}

// ── seed loading (Lua) ──────────────────────────────────────────────────────
// Project rules 2 and 3: configuration is a Lua file, and Lua I/O goes through
// lua_engine — never std::ifstream. The seed is
//     return { tree = { ["/xpath"] = value, … } }
// with values of any scalar Lua type; each is rendered to the string the gNMI
// TypedValue will carry.

/// Render one Lua scalar as the value string for a leaf.
std::string scalar_to_string(const lua_file::value_type& v) {
    if (std::holds_alternative<std::string>(v))   return std::get<std::string>(v);
    if (std::holds_alternative<bool>(v))          return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<std::int32_t>(v))  return std::to_string(std::get<std::int32_t>(v));
    if (std::holds_alternative<std::uint32_t>(v)) return std::to_string(std::get<std::uint32_t>(v));
    if (std::holds_alternative<double>(v)) {
        // Lua numbers arrive as double; print integral values without the ".0"
        // so an mtu reads "1500" rather than "1500.000000".
        const double d = std::get<double>(v);
        if (d == static_cast<double>(static_cast<long long>(d)))
            return std::to_string(static_cast<long long>(d));
        return std::to_string(d);
    }
    return {};   // nil / unsupported
}

/// Load `path` into `tree`. Returns false when the file is missing or malformed
/// (the daemon then serves an empty tree rather than refusing to start).
bool load_seed(const std::string& path, GnmiTree& tree, std::string& err) {
    lua_file cfg;
    cfg.process_create_luafile(path);

    // process_create_luafile only inserts on a successful load; absence = failure.
    const auto it = cfg.commands().find(path);
    if (it == cfg.commands().end()) {
        err = "cannot load Lua seed: " + path;
        return false;
    }

    const auto members = it->second.members.find("tree");
    if (members == it->second.members.end()) {
        err = "seed has no `tree` table (expected: return { tree = { … } })";
        return false;
    }
    const auto* tbl = std::get_if<std::shared_ptr<lua_file::table_type>>(&members->second);
    if (!tbl || !*tbl) {
        err = "`tree` is not a table";
        return false;
    }

    for (const auto& [xpath, entry] : (*tbl)->members) {
        if (xpath.empty() || xpath.front() != '/') continue;   // not an xpath
        if (const auto* val = std::get_if<lua_file::value_type>(&entry))
            tree.set(xpath, scalar_to_string(*val));
    }
    return true;
}

// ── gNMI handlers ───────────────────────────────────────────────────────────
// Signature is grpc_session::unary_handler_t: request bytes in,
// {grpc_status, response bytes} out. 0 = OK; non-zero closes the RPC with that
// gRPC status. NB: grace-server's send_unary_response only emits the
// `grpc-status` trailer, never `grpc-message`, so the string returned alongside
// a non-zero status does NOT reach the client — the status code is the whole
// signal. (zerotouch's format_get/format_set render the code by name.)

std::pair<int, std::string> handle_get(const std::string& req_pb) {
    gnmi::GetRequest req;
    if (!req.ParseFromString(req_pb)) {
        log("[Get] INVALID_ARGUMENT — malformed request");
        return {3, ""};   // INVALID_ARGUMENT
    }

    const std::string role =
        req.prefix().target().empty() ? "VIEWER" : req.prefix().target();

    gnmi::GetResponse resp;
    std::size_t found = 0;
    std::string asked;

    for (const auto& p : req.path()) {
        const std::string xpath = gnmi_util::path_to_string(p);
        asked += (asked.empty() ? "" : ", ") + xpath;

        const auto rows = g_tree.lookup(xpath);
        // One Notification per requested path keeps the correlation obvious in
        // a packet capture; each matched leaf becomes an Update inside it.
        auto* notif = resp.add_notification();
        notif->set_timestamp(now_ns());
        for (const auto& row : rows) {
            auto* u = notif->add_update();
            *u->mutable_path() = gnmi_util::parse_yang_path(row.xpath);
            gnmi_util::set_typed_value(u->mutable_val(), row.value);
            ++found;
        }
    }

    if (req.path_size() > 0 && found == 0) {
        log("[Get] role=" + role + " NOT_FOUND paths=[" + asked + "]");
        return {5, ""};   // NOT_FOUND — nothing in the tree matched
    }

    log("[Get] role=" + role + " paths=" + std::to_string(req.path_size()) +
        " leaves=" + std::to_string(found) + " [" + asked + "]");

    std::string out;
    resp.SerializeToString(&out);
    return {0, out};
}

std::pair<int, std::string> handle_set(const std::string& req_pb) {
    gnmi::SetRequest req;
    if (!req.ParseFromString(req_pb)) {
        log("[Set] INVALID_ARGUMENT — malformed request");
        return {3, ""};
    }

    // RBAC, same convention as grace-server: the role rides in prefix.target
    // and anything other than ADMIN is refused. This is the SECOND gate — the
    // first is zerotouch's own "GNMI SET needs an Admin session".
    const std::string role = req.prefix().target();
    if (role != "ADMIN") {
        log("[Set] PERMISSION_DENIED role=" +
            (role.empty() ? std::string("VIEWER(default)") : role));
        return {7, ""};   // PERMISSION_DENIED
    }

    gnmi::SetResponse resp;
    resp.set_timestamp(now_ns());

    for (const auto& u : req.update()) {
        const std::string xpath = gnmi_util::path_to_string(u.path());
        const std::string value = gnmi_util::typed_value_to_string(u.val());
        g_tree.set(xpath, value);
        auto* r = resp.add_response();
        *r->mutable_path() = u.path();
        r->set_op(gnmi::UpdateResult::UPDATE);
        log("[Set] UPDATE " + xpath + " = " + value);
    }
    for (const auto& u : req.replace()) {
        const std::string xpath = gnmi_util::path_to_string(u.path());
        const std::string value = gnmi_util::typed_value_to_string(u.val());
        g_tree.erase(xpath);          // replace = drop the subtree, then write
        g_tree.set(xpath, value);
        auto* r = resp.add_response();
        *r->mutable_path() = u.path();
        r->set_op(gnmi::UpdateResult::REPLACE);
        log("[Set] REPLACE " + xpath + " = " + value);
    }
    for (const auto& d : req.delete_()) {
        const std::string xpath = gnmi_util::path_to_string(d);
        const std::size_t n = g_tree.erase(xpath);
        auto* r = resp.add_response();
        *r->mutable_path() = d;
        r->set_op(gnmi::UpdateResult::DELETE);
        log("[Set] DELETE " + xpath + " (" + std::to_string(n) + " leaf/leaves)");
    }

    log("[Set] role=ADMIN ok — tree now " + std::to_string(g_tree.size()) +
        " leaf/leaves");

    std::string out;
    resp.SerializeToString(&out);
    return {0, out};
}

// ── reactor plumbing ────────────────────────────────────────────────────────
// grace-server's own `server`/`connected_client` pair can't be reused here: it
// hardcodes connected_client in handle_connect and registers its (stub) gNMI
// handlers in a private method, with m_grpc private too. So we mirror the same
// two-class shape over the same base and register our own handlers.

class sim_server;

/// One accepted connection: owns a grpc_session and feeds socket bytes to it.
class sim_conn : public evt_io {
public:
    sim_conn(struct bufferevent* bev, const std::string& peer, sim_server* parent)
      : evt_io(bev, peer), m_parent(parent),
        m_grpc(std::make_unique<grpc_session>(
            [this](const char* d, std::size_t n) { tx(d, n); })) {
        m_grpc->register_unary("/gnmi.gNMI/Get", handle_get);
        m_grpc->register_unary("/gnmi.gNMI/Set", handle_set);
    }

    std::int32_t handle_read(const std::int32_t&, const std::string& data,
                             const bool& dry_run) override {
        if (dry_run) return 0;   // "can you handle this?" — always, it's HTTP/2
        // recv() decodes HTTP/2 + gRPC framing, dispatches to the handlers above
        // and flushes any response frames back through tx().
        const ssize_t consumed = m_grpc->recv(
            reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        if (consumed < 0) log("http2/grpc recv error: " + std::to_string(consumed));
        return static_cast<std::int32_t>(consumed);
    }

    std::int32_t handle_event(const std::int32_t&, const std::uint16_t&) override {
        return 0;
    }
    std::int32_t handle_write(const std::int32_t&) override { return 0; }

    // Destroys `this` via the parent's map — no member access may follow.
    std::int32_t handle_close(const std::int32_t& channel) override;

private:
    sim_server*                   m_parent;
    std::unique_ptr<grpc_session> m_grpc;
};

/// The listener: accepts connections and owns them.
class sim_server : public evt_io {
public:
    sim_server(const std::string& host, std::uint16_t port) : evt_io(host, port) {}

    std::int32_t handle_connect(const std::int32_t& channel,
                                const std::string& peer_host) override {
        // wrap_accepted() gives a plain-TCP bufferevent (no TLS ctx set).
        auto* bev = wrap_accepted(channel);
        auto res = m_conns.emplace(
            channel, std::make_unique<sim_conn>(bev, peer_host, this));
        if (!res.second) return -1;
        log("client connected: " + peer_host);
        return 0;
    }

    std::int32_t handle_accept(const std::int32_t&, const std::string&) override {
        return 0;
    }

    std::int32_t handle_close(const std::int32_t& channel) override {
        return static_cast<std::int32_t>(m_conns.erase(channel));
    }

private:
    std::unordered_map<std::int32_t, std::unique_ptr<sim_conn>> m_conns;
};

std::int32_t sim_conn::handle_close(const std::int32_t& channel) {
    if (m_parent) m_parent->handle_close(channel);   // destroys this
    return 0;
}

void usage() {
    std::printf(
        "zt-gnmi-simd — gNMI server for the zero-touch simulation\n"
        "\nUsage: zt-gnmi-simd [--listen=HOST:PORT] [--seed=PATH] [--quiet]\n"
        "  --listen=HOST:PORT  bind address (default 0.0.0.0:50051)\n"
        "  --seed=PATH         Lua seed tree, return { tree = { [\"/xpath\"]=v } }\n"
        "                      (default /etc/zerotouch/gnmi-tree.lua)\n"
        "  --quiet             do not log each RPC\n"
        "\nServes gNMI Get/Set over plaintext gRPC. Set requires the caller's\n"
        "prefix.target to be \"ADMIN\" (zerotouch's LocalGnmiSink sends it).\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "0.0.0.0";
    std::uint16_t port = 50051;
    std::string seed = "/etc/zerotouch/gnmi-tree.lua";

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--listen=", 0) == 0) {
            const std::string hp = a.substr(9);
            const auto colon = hp.rfind(':');
            if (colon == std::string::npos) {
                std::fprintf(stderr, "--listen needs HOST:PORT\n");
                return 2;
            }
            host = hp.substr(0, colon);
            port = static_cast<std::uint16_t>(std::stoi(hp.substr(colon + 1)));
        } else if (a.rfind("--seed=", 0) == 0) {
            seed = a.substr(7);
        } else if (a == "--quiet") {
            g_verbose = false;
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg '%s'\n", a.c_str());
            usage();
            return 2;
        }
    }

    if (std::string err; !load_seed(seed, g_tree, err))
        std::cout << "zt-gnmi-simd: " << err
                  << " — starting with an empty tree\n";

    std::cout <<
        "\n"
        "   _____               _____                _\n"
        "  |__  /___ _ __ ___  |_   _|__  _   _  ___| |__\n"
        "    / // _ \\ '__/ _ \\   | |/ _ \\| | | |/ __| '_ \\\n"
        "   / /|  __/ | | (_) |  | | (_) | |_| | (__| | | |\n"
        "  /____\\___|_|  \\___/   |_|\\___/ \\__,_|\\___|_| |_|\n"
        "\n"
        "  gNMI server (simulated device) — plaintext gRPC, no TLS\n";
    std::cout << "  listening on " << host << ":" << port
              << "   seed=" << seed
              << "   " << g_tree.size() << " leaf/leaves\n";
    for (const auto& [k, v] : g_tree.all())
        std::cout << "    " << k << " = " << v << "\n";
    std::cout << "  Set requires prefix.target=ADMIN. Waiting for clients…\n"
              << std::flush;

    sim_server srv(host, port);
    run_evt_loop{}();
    return 0;
}
