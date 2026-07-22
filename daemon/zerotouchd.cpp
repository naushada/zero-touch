#include "zerotouchd.hpp"

#include <fstream>
#include <vector>

#include <ace/Log_Msg.h>
#include <ace/OS_NS_sys_time.h>
#include <ace/Reactor.h>
#include <ace/Time_Value.h>

#include <nlohmann/json.hpp>

#include "data_store/stats_publisher.hpp"

#include "smsctl/executor.hpp"
#include "smsctl/parser.hpp"

/**
 * @file zerotouchd.cpp
 * @brief zero-touchd daemon wiring. Built when ZT_BUILD_DAEMON is ON.
 *        See zerotouchd.hpp / DESIGN.md.
 */

namespace zerotouch {

namespace {

using json = nlohmann::json;

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else if (c != ' ' && c != '\t')  { cur.push_back(c); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/// The live ds/filesystem implementation of the classic executor's world —
/// ported from iot-smsctld (the reusable engine is smsctl::Executor; this glue
/// is daemon-specific). ds ACLs apply; privileged actions are trigger files.
class LiveSink : public smsctl::DsSink {
public:
    explicit LiveSink(data_store::Client& ds) : m_ds(ds) {}

    bool set(const std::string& key, const std::string& value) override {
        auto rs = m_ds.set(key, data_store::Value{value});
        if (!rs.ok)
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [zerotouch] ds set(%C) failed: %C\n"),
                       key.c_str(), rs.err.c_str()));
        return rs.ok;
    }

    std::optional<std::string> get(const std::string& key) override {
        std::vector<data_store::Client::GetResult> got;
        if (!m_ds.get({key}, got).ok || got.empty() || !got[0].has_value)
            return std::nullopt;
        return data_store::to_string(got[0].value);
    }

    bool arm_trigger(const std::string& path, const std::string& content) override {
        std::ofstream trig(path, std::ios::trunc);
        if (!trig) {
            ACE_ERROR((LM_ERROR,
                       ACE_TEXT("%D [zerotouch] cannot arm trigger %C "
                                "(perms or non-Yocto host)\n"), path.c_str()));
            return false;
        }
        trig << content;
        return trig.good();
    }

    std::uint64_t now_ms() override {
        const ACE_Time_Value tv = ACE_OS::gettimeofday();
        return static_cast<std::uint64_t>(tv.sec()) * 1000ULL +
               static_cast<std::uint64_t>(tv.usec() / 1000);
    }

private:
    data_store::Client& m_ds;
};

} // namespace

ZeroTouchClient::ZeroTouchClient(Config cfg)
  : m_cfg(std::move(cfg)),
    m_gex(m_sink, [this](const std::string& s) { return sender_access(s); }),
    m_transport(m_ds) {}

void ZeroTouchClient::load_config_from_ds() {
    auto get_str = [this](const char* key) -> std::string {
        std::vector<data_store::Client::GetResult> got;
        if (m_ds.get({std::string(key)}, got).ok && !got.empty() && got[0].has_value)
            if (auto s = data_store::to_string(got[0].value)) return *s;
        return {};
    };
    auto get_u32 = [this](const char* key, std::uint32_t dflt) -> std::uint32_t {
        std::vector<data_store::Client::GetResult> got;
        if (m_ds.get({std::string(key)}, got).ok && !got.empty() && got[0].has_value)
            if (auto n = data_store::to_int32(got[0].value))
                if (*n > 0) return static_cast<std::uint32_t>(*n);
        return dflt;
    };
    auto get_bool = [this](const char* key, bool dflt) -> bool {
        std::vector<data_store::Client::GetResult> got;
        if (m_ds.get({std::string(key)}, got).ok && !got.empty() && got[0].has_value)
            if (auto b = data_store::to_bool(got[0].value)) return *b;
        return dflt;
    };

    m_enabled   = get_bool("zerotouch.enabled", false);
    m_gnmi_port = static_cast<std::uint16_t>(get_u32("zerotouch.gnmi.port", 50051));

    LocalGnmiSink::Config gc = m_sink.config();
    gc.port = m_gnmi_port;
    m_sink.set_config(std::move(gc));

    smsctl::Config c;
    c.session_ttl_sec  = get_u32("zerotouch.session.ttl.sec", 600);
    c.lockout_failures = get_u32("zerotouch.lockout.failures", 5);
    c.lockout_sec      = get_u32("zerotouch.lockout.sec", 900);
    c.allowed_numbers  = split_csv(get_str("zerotouch.allowed.numbers"));
    m_sessions.set_config(std::move(c));
}

bool ZeroTouchClient::lookup_account(const std::string& id, smsctl::Account& out) {
    std::vector<data_store::Client::GetResult> got;
    if (id == "admin") {
        if (!m_ds.get({std::string("auth.users.admin.password.hash")}, got).ok ||
            got.empty() || !got[0].has_value)
            return false;
        auto h = data_store::to_string(got[0].value);
        if (!h || h->empty()) return false;
        out.id     = "admin";
        out.hash   = *h;
        out.access = "Admin";
        got.clear();
        if (m_ds.get({std::string("auth.users.admin.access")}, got).ok &&
            !got.empty() && got[0].has_value)
            if (auto a = data_store::to_string(got[0].value))
                if (*a == "Viewer") out.access = "Viewer";
        return true;
    }

    if (!m_ds.get({std::string("auth.users.accounts")}, got).ok ||
        got.empty() || !got[0].has_value)
        return false;
    auto s = data_store::to_string(got[0].value);
    if (!s || s->empty()) return false;
    try {
        const auto arr = json::parse(*s);
        if (!arr.is_array()) return false;
        for (const auto& u : arr) {
            if (!u.is_object()) continue;
            if (u.value("id", std::string()) != id) continue;
            out.id     = id;
            out.hash   = u.value("hash", std::string());
            out.access = u.value("access", std::string("Viewer"));
            return !out.hash.empty();
        }
    } catch (const std::exception&) {
        return false;
    }
    return false;
}

Access ZeroTouchClient::sender_access(const std::string& sender) {
    const std::uint64_t now =
        static_cast<std::uint64_t>(ACE_OS::gettimeofday().sec());
    const smsctl::Account* acc = m_sessions.session(sender, now);
    if (!acc) return Access::None;
    return acc->access == "Admin" ? Access::Admin : Access::Viewer;
}

bool ZeroTouchClient::sender_allowed(const std::string& sender) {
    // Enabled AND on the allowlist — otherwise the Bridge drops in silence.
    return m_enabled && m_sessions.sender_allowed(sender);
}

std::string ZeroTouchClient::classic_reply(const std::string& sender,
                                           const std::string& text) {
    const smsctl::Command cmd = smsctl::parse(text);
    if (cmd.kind == smsctl::Kind::NotACommand) return {};   // ordinary text — silent

    const ACE_Time_Value tv = ACE_OS::gettimeofday();
    const std::uint64_t now  = static_cast<std::uint64_t>(tv.sec());
    const std::uint64_t seed = static_cast<std::uint64_t>(tv.usec()) * 2654435761ULL
                             + ++m_handled;

    LiveSink sink(m_ds);
    smsctl::Executor ex(sink, m_sessions,
                        [this](const std::string& id, smsctl::Account& out) {
                            return lookup_account(id, out);
                        });
    const std::string reply = ex.handle(cmd, sender, now, seed);

    // NEVER log or publish command arguments (a LOGIN password / WiFi PSK).
    // Keyword + outcome only.
    const bool ok = reply.compare(0, 2, "OK") == 0;
    ACE_DEBUG((LM_INFO, ACE_TEXT("%D [zerotouch] %C from %C -> %C\n"),
               cmd.keyword(), sender.c_str(), ok ? "ok" : "err"));

    std::vector<data_store::KV> batch;
    batch.emplace_back("zerotouch.last.sender", data_store::Value{sender});
    batch.emplace_back("zerotouch.last.cmd",    data_store::Value{std::string(cmd.keyword())});
    batch.emplace_back("zerotouch.last.result", data_store::Value{std::string(ok ? "ok" : "err")});
    batch.emplace_back("zerotouch.last.ts",     data_store::Value{std::to_string(now)});
    batch.emplace_back("zerotouch.sessions",    data_store::Value{std::to_string(m_sessions.session_count())});
    batch.emplace_back("zerotouch.version",     data_store::Value{std::to_string(m_handled)});
    m_ds.set_volatile(batch);

    return reply;
}

void ZeroTouchClient::publish_state() {
    std::vector<data_store::KV> batch;
    batch.emplace_back("zerotouch.state",
                       data_store::Value{std::string(m_enabled ? "listening"
                                                               : "disabled")});
    batch.emplace_back("zerotouch.sessions",
                       data_store::Value{std::to_string(m_sessions.session_count())});
    if (!m_ds.set_volatile(batch).ok)
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [zerotouch] ds set(zerotouch.state) failed\n")));
}

int ZeroTouchClient::handle_timeout(const ACE_Time_Value&, const void*) {
    const std::uint64_t now =
        static_cast<std::uint64_t>(ACE_OS::gettimeofday().sec());
    const std::size_t before = m_sessions.session_count();
    m_sessions.sweep(now);
    if (m_sessions.session_count() != before) publish_state();
    return 0;
}

int ZeroTouchClient::handle_exception(ACE_HANDLE) {
    bool cfg_dirty;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        cfg_dirty = m_cfg_dirty; m_cfg_dirty = false;
    }
    if (cfg_dirty) {
        const bool was = m_enabled;
        load_config_from_ds();
        if (was != m_enabled)
            ACE_DEBUG((LM_INFO, ACE_TEXT("%D [zerotouch] %C\n"),
                       m_enabled ? "enabled — listening for IOT commands"
                                 : "disabled — inbound SMS ignored"));
        publish_state();
    }
    return 0;
}

int ZeroTouchClient::run() {
    if (!m_ds.connect(m_cfg.ds_sock).ok)
        ACE_ERROR_RETURN((LM_ERROR, ACE_TEXT("%D [zerotouch] ds connect failed\n")), 1);

    load_config_from_ds();

    // Wire the transport → engine. Bridge::start() registers the sms.version
    // watch and baselines the replay guard inside DsSmsTransport.
    m_bridge = std::make_unique<Bridge>(
        m_transport, m_gex, smsctl::tokenize,
        [this](const std::string& s, const std::string& t) { return classic_reply(s, t); },
        [this](const std::string& s) { return sender_allowed(s); });
    m_bridge->start();

    // Config watch: any zerotouch.* change flags dirty + wakes THIS handler.
    static const char* const kCfgKeys[] = {
        "zerotouch.enabled",      "zerotouch.gnmi.port",
        "zerotouch.allowed.numbers", "zerotouch.session.ttl.sec",
        "zerotouch.lockout.failures", "zerotouch.lockout.sec",
    };
    std::vector<std::string> keys;
    for (const char* k : kCfgKeys) keys.emplace_back(k);
    m_ds.watch(keys, [this](const data_store::Client::Event&) {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_cfg_dirty = true;
        }
        ACE_Reactor::instance()->notify(this);
    });

    publish_state();

    ACE_Reactor::instance()->schedule_timer(
        this, nullptr, ACE_Time_Value(1), ACE_Time_Value(1));

    ACE_DEBUG((LM_INFO,
        ACE_TEXT("%D [zerotouch] up: %C, gnmi 127.0.0.1:%u, session ttl %us, "
                 "allowlist %u number(s)\n"),
        m_enabled ? "enabled" : "disabled (zerotouch.enabled=false)",
        static_cast<unsigned>(m_gnmi_port),
        m_sessions.config().session_ttl_sec,
        static_cast<unsigned>(m_sessions.config().allowed_numbers.size())));

    m_ds.set(std::string("services.zerotouch.state"),
             data_store::Value{std::string("running")});
    static data_store::StatsPublisher stats("services.zerotouch",
        [this](const std::vector<data_store::KV>& kv) { m_ds.set(kv); });
    stats.open(data_store::StatsPublisher::STATS_FLUSH_SEC, false);

    ACE_Reactor::instance()->run_reactor_event_loop();
    return 0;
}

} // namespace zerotouch
