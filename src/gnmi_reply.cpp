#include "zerotouch/gnmi_reply.hpp"

/**
 * @file gnmi_reply.cpp
 * @brief GnmiResult → single reply SMS. Pure; see gnmi_reply.hpp.
 */

namespace zerotouch {

std::string clamp_sms(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    if (max <= 3) return s.substr(0, max);
    return s.substr(0, max - 3) + "...";
}

std::string format_get(const GnmiResult& r) {
    if (!r.ok) {
        std::string msg = r.grpc_message.empty() ? "failed" : r.grpc_message;
        return clamp_sms("ERR GNMI GET " + msg);
    }
    std::string body = "OK GNMI GET ";
    bool first = true;
    for (const auto& p : r.paths) {
        if (!first) body += "; ";
        first = false;
        // A per-path error (e.g. denylisted/sensitive) reports the error, never
        // the value — so a secret cannot leak through a GET reply.
        body += p.xpath + "=" + (p.error.empty() ? p.value : ("<" + p.error + ">"));
    }
    return clamp_sms(body);
}

std::string format_set(const GnmiResult& r, std::size_t n) {
    if (!r.ok) {
        std::string msg = r.grpc_message.empty() ? "failed" : r.grpc_message;
        return clamp_sms("ERR GNMI SET " + msg);
    }
    return clamp_sms("OK GNMI SET " + std::to_string(n) + " path(s) updated");
}

} // namespace zerotouch
