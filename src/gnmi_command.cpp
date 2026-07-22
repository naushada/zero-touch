#include "zerotouch/gnmi_command.hpp"

#include <algorithm>
#include <cctype>

/**
 * @file gnmi_command.cpp
 * @brief Parser for the `IOT GNMI GET/SET …` grammar. Pure; see gnmi_command.hpp.
 */

namespace zerotouch {

namespace {

bool ieq(const std::string& a, const char* b) {
    std::string lb = b;
    if (a.size() != lb.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != lb[i]) return false;
    return true;
}

/// Split a comma-separated list. Tokens are already de-quoted by
/// smsctl::tokenize, so an SSID/value containing a comma must be sent as
/// separate tokens — the CSV split here is on the single token's commas.
std::vector<std::string> csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else          { cur.push_back(c); }
    }
    out.push_back(cur);
    return out;
}

} // namespace

GnmiCommand parse_gnmi(const std::vector<std::string>& t) {
    GnmiCommand cmd;

    // Need at least: IOT GNMI <op> <arg>
    if (t.size() < 2 || !ieq(t[0], "iot") || !ieq(t[1], "gnmi"))
        return cmd;   // NotGnmi — hand back to the smsctl engine

    if (t.size() < 3) {
        cmd.kind  = GnmiKind::Unknown;
        cmd.error = "gnmi: expected GET or SET";
        return cmd;
    }

    if (ieq(t[2], "get")) {
        if (t.size() < 4) {
            cmd.kind = GnmiKind::Unknown;
            cmd.error = "gnmi get: missing xpath";
            return cmd;
        }
        cmd.kind   = GnmiKind::Get;
        cmd.xpaths = csv(t[3]);
        return cmd;
    }

    if (ieq(t[2], "set")) {
        if (t.size() < 5) {
            cmd.kind = GnmiKind::Unknown;
            cmd.error = "gnmi set: expected <xpaths> <values>";
            return cmd;
        }
        const auto xs = csv(t[3]);
        const auto vs = csv(t[4]);
        if (xs.size() != vs.size()) {
            cmd.kind = GnmiKind::Unknown;
            cmd.error = "gnmi set: xpath/value count mismatch";
            return cmd;
        }
        cmd.kind = GnmiKind::Set;
        for (std::size_t i = 0; i < xs.size(); ++i)
            cmd.updates.emplace_back(xs[i], vs[i]);
        return cmd;
    }

    cmd.kind  = GnmiKind::Unknown;
    cmd.error = "gnmi: unknown operation";
    return cmd;
}

} // namespace zerotouch
