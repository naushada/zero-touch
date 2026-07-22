#include "zerotouch/config.hpp"

#include <cctype>
#include <sstream>

/**
 * @file config.cpp
 * @brief `key = value` config parser. Pure; see config.hpp.
 */

namespace zerotouch {

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool to_bool(const std::string& v) {
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

std::uint32_t to_u32(const std::string& v, std::uint32_t dflt) {
    try {
        const long n = std::stol(v);
        if (n > 0) return static_cast<std::uint32_t>(n);
    } catch (const std::exception&) {}
    return dflt;
}

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

} // namespace

AppConfig parse_config(const std::string& text) {
    AppConfig c;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        // Strip a trailing comment; skip blanks.
        if (const auto h = line.find('#'); h != std::string::npos) line.erase(h);
        line = trim(line);
        if (line.empty()) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;   // not key=value — ignore
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        if      (key == "enabled")          c.enabled          = to_bool(val);
        else if (key == "gnmi.host")        c.gnmi_host        = val;
        else if (key == "gnmi.port")        c.gnmi_port        = static_cast<std::uint16_t>(to_u32(val, c.gnmi_port));
        else if (key == "allowed.numbers")  c.allowed_numbers  = split_csv(val);
        else if (key == "session.ttl.sec")  c.session_ttl_sec  = to_u32(val, c.session_ttl_sec);
        else if (key == "lockout.failures") c.lockout_failures = to_u32(val, c.lockout_failures);
        else if (key == "lockout.sec")      c.lockout_sec      = to_u32(val, c.lockout_sec);
        else if (key == "modem.dev")        c.modem_dev        = val;
        else if (key == "modem.baud")       c.modem_baud       = to_u32(val, c.modem_baud);
        else if (key == "users.file")       c.users_file       = val;
        // unknown keys: ignored
    }
    return c;
}

} // namespace zerotouch
