/// zero-touchd-standalone — ds-free gNMI-over-SMS appliance (one daemon).
///
/// No ds-server, no cellular-client: SMS rides the modem directly (AtModem),
/// config + users come from files, and the classic smsctl commands act directly
/// on the modem / OS (DirectActionSink). gNMI GET/SET hit the device-local gNMI
/// server. Inert until `enabled = true` in the config file. See DESIGN.md.

#include <cstdint>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <sys/reboot.h>
#include <unistd.h>

#include <ace/Log_Msg.h>
#include <ace/Reactor.h>
#include <ace/Time_Value.h>

#include "smsctl/executor.hpp"
#include "smsctl/parser.hpp"
#include "smsctl/session.hpp"

#include "zerotouch/at_modem_transport.hpp"
#include "zerotouch/modem_factory.hpp"
#include "zerotouch/bridge.hpp"
#include "zerotouch/config.hpp"
#include "zerotouch/direct_action_sink.hpp"
#include "zerotouch/gnmi_executor.hpp"
#include "zerotouch/local_gnmi_sink.hpp"
#include "zerotouch/users.hpp"

namespace zerotouch {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::uint64_t now_s() { return static_cast<std::uint64_t>(std::time(nullptr)); }

} // namespace

/// Owns the reactor + the shared SessionStore; the rest is wired in run().
class StandaloneClient : public ACE_Event_Handler {
public:
    explicit StandaloneClient(std::string config_path)
      : m_config_path(std::move(config_path)) {}

    int run();
    int handle_timeout(const ACE_Time_Value&, const void*) override;

private:
    std::string          m_config_path;
    AppConfig            m_config;
    UserStore            m_users;
    smsctl::SessionStore m_sessions;
    std::uint64_t        m_handled = 0;
};

int StandaloneClient::handle_timeout(const ACE_Time_Value&, const void*) {
    m_sessions.sweep(now_s());
    return 0;
}

int StandaloneClient::run() {
    m_config = parse_config(read_file(m_config_path));
    m_users  = UserStore::parse(read_file(m_config.users_file));

    smsctl::Config sc;
    sc.session_ttl_sec  = m_config.session_ttl_sec;
    sc.lockout_failures = m_config.lockout_failures;
    sc.lockout_sec      = m_config.lockout_sec;
    sc.allowed_numbers  = m_config.allowed_numbers;
    m_sessions.set_config(sc);

    // ── backends behind the seams ───────────────────────────────────────────
    std::unique_ptr<IModem> modem_ptr = make_modem(m_config);   // config → vendor
    IModem&          modem = *modem_ptr;
    AtModemTransport transport(modem);
    LocalGnmiSink    sink(LocalGnmiSink::Config{m_config.gnmi_host, m_config.gnmi_port});
    DirectActionSink action(modem, [] { ::sync(); ::reboot(RB_AUTOBOOT); return true; });

    auto lookup = [this](const std::string& id, smsctl::Account& out) {
        User u;
        if (!m_users.lookup(id, u)) return false;
        out.id = u.id; out.hash = u.hash; out.access = u.access;
        return true;
    };
    auto authfn = [this](const std::string& sender) -> Access {
        const smsctl::Account* a = m_sessions.session(sender, now_s());
        if (!a) return Access::None;
        return a->access == "Admin" ? Access::Admin : Access::Viewer;
    };
    auto allow = [this](const std::string& sender) {
        return m_config.enabled && m_sessions.sender_allowed(sender);
    };
    auto fallback = [&](const std::string& sender, const std::string& text) -> std::string {
        const smsctl::Command cmd = smsctl::parse(text);
        if (cmd.kind == smsctl::Kind::NotACommand) return {};
        // No iot daemons to act on these on a standalone box.
        if (cmd.kind == smsctl::Kind::Wifi || cmd.kind == smsctl::Kind::FactoryReset)
            return std::string("ERR ") + cmd.keyword() + ": not supported on this device";
        const std::uint64_t now  = now_s();
        const std::uint64_t seed  = now * 2654435761ULL + (++m_handled);
        smsctl::Executor ex(action, m_sessions, lookup);
        return ex.handle(cmd, sender, now, seed);
    };

    GnmiExecutor gex(sink, authfn);
    Bridge bridge(transport, gex, smsctl::tokenize, fallback, allow);
    bridge.start();   // → transport.start() → modem.start() (opens serial + poll)

    ACE_Reactor::instance()->schedule_timer(
        this, nullptr, ACE_Time_Value(1), ACE_Time_Value(1));   // session sweep

    ACE_DEBUG((LM_INFO,
        ACE_TEXT("%D [zerotouch] standalone up: %C, gnmi %C:%u, modem %C, "
                 "allowlist %u\n"),
        m_config.enabled ? "enabled" : "disabled (enabled=false)",
        m_config.gnmi_host.c_str(), static_cast<unsigned>(m_config.gnmi_port),
        m_config.modem_dev.c_str(),
        static_cast<unsigned>(m_config.allowed_numbers.size())));

    ACE_Reactor::instance()->run_reactor_event_loop();
    return 0;
}

} // namespace zerotouch

namespace {
void usage() {
    std::printf(
        "zero-touchd-standalone — gNMI provisioning over SMS (ds-free appliance)\n"
        "\nUsage: zero-touchd-standalone [--config=PATH] [--help]\n"
        "  --config=PATH   config file (default /etc/zerotouch/zerotouch.conf)\n");
}
} // namespace

int main(int argc, char** argv) {
    std::string config = "/etc/zerotouch/zerotouch.conf";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a.rfind("--config=", 0) == 0) config = a.substr(9);
        else if (a == "--help" || a == "-h")   { usage(); return 0; }
        else { std::fprintf(stderr, "unknown arg '%s'\n", a.c_str()); usage(); return 2; }
    }
    zerotouch::StandaloneClient client(std::move(config));
    return client.run();
}
