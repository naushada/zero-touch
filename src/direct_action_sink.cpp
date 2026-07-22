#include "zerotouch/direct_action_sink.hpp"

#include <chrono>
#include <string>
#include <vector>

/**
 * @file direct_action_sink.cpp
 * @brief Classic-command backend over the modem's AT channel + a reboot syscall.
 *        See direct_action_sink.hpp.
 */

namespace zerotouch {

namespace {

/// Split the payload of a `+XXX: a,b,c` response line into its comma fields.
/// Returns the fields after the first ':' (trimmed of spaces / quotes).
std::vector<std::string> at_fields(const std::string& line) {
    std::vector<std::string> out;
    const auto colon = line.find(':');
    if (colon == std::string::npos) return out;
    std::string cur;
    auto flush = [&] {
        // trim spaces + surrounding quotes
        std::size_t b = 0, e = cur.size();
        while (b < e && (cur[b] == ' ' || cur[b] == '"')) ++b;
        while (e > b && (cur[e - 1] == ' ' || cur[e - 1] == '"' || cur[e - 1] == '\r')) --e;
        out.push_back(cur.substr(b, e - b));
        cur.clear();
    };
    for (std::size_t i = colon + 1; i < line.size(); ++i) {
        if (line[i] == ',') flush();
        else                cur.push_back(line[i]);
    }
    flush();
    return out;
}

/// 3GPP registration <stat> → short label (matches the old ds cell.reg values).
std::string reg_label(const std::string& stat) {
    if (stat == "1") return "home";
    if (stat == "5") return "roam";
    if (stat == "2") return "searching";
    if (stat == "3") return "denied";
    if (stat == "0") return "off";
    return "-";
}

} // namespace

bool DirectActionSink::set(const std::string& key, const std::string& value) {
    if (key == "cell.apn") {
        // PDP context 1, IPv4. The radio cycle (cell.reset.request) applies it.
        return m_modem.at("AT+CGDCONT=1,\"IP\",\"" + value + "\"").ok;
    }
    if (key == "cell.reset.request") {
        // CFUN cycle — deregister then re-register the radio.
        const bool down = m_modem.at("AT+CFUN=0").ok;
        const bool up   = m_modem.at("AT+CFUN=1").ok;
        return down && up;
    }
    // Any other ds key the executor might write has no standalone action.
    return true;
}

std::optional<std::string> DirectActionSink::get(const std::string& key) {
    if (key == "cell.reg" || key == "cell.reg.cs") {
        const auto r = m_modem.at("AT+CREG?");
        if (!r.ok) return std::nullopt;
        const auto f = at_fields(r.line_with("+CREG:"));  // <n>,<stat>
        if (f.size() < 2) return std::nullopt;
        return reg_label(f[1]);
    }
    if (key == "cell.signal.dbm") {
        const auto r = m_modem.at("AT+CSQ");
        if (!r.ok) return std::nullopt;
        const auto f = at_fields(r.line_with("+CSQ:"));   // <rssi>,<ber>
        if (f.empty() || f[0] == "99") return std::nullopt;
        try {
            return std::to_string(-113 + 2 * std::stoi(f[0]));
        } catch (const std::exception&) { return std::nullopt; }
    }
    if (key == "cell.ip") {
        const auto r = m_modem.at("AT+CGPADDR=1");
        if (!r.ok) return std::nullopt;
        const auto f = at_fields(r.line_with("+CGPADDR:")); // <cid>,<addr>
        if (f.size() < 2 || f[1].empty() || f[1] == "0.0.0.0") return std::nullopt;
        return f[1];
    }
    // vpn.state / wifi.assoc.ssid / net.iface.active / … → absent → "-".
    return std::nullopt;
}

bool DirectActionSink::arm_trigger(const std::string& path, const std::string&) {
    if (path == smsctl::kRebootTrigger)
        return m_reboot ? m_reboot() : false;
    // factory-reset is rejected by the daemon; any other trigger is unsupported.
    return false;
}

std::uint64_t DirectActionSink::now_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

} // namespace zerotouch
