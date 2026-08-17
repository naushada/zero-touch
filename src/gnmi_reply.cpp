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

std::string grpc_status_text(int code) {
    // Canonical gRPC codes, lowercased and kept terse — this lands in a 160-char
    // SMS. -1 is gnmi_client's "never got a status" (connect/TLS/parse failure).
    switch (code) {
    case -1: return "transport error";
    case  1: return "cancelled";
    case  2: return "unknown";
    case  3: return "invalid argument";
    case  4: return "deadline exceeded";
    case  5: return "not found";
    case  6: return "already exists";
    case  7: return "permission denied";
    case  8: return "resource exhausted";
    case  9: return "failed precondition";
    case 10: return "aborted";
    case 11: return "out of range";
    case 12: return "unimplemented";
    case 13: return "internal";
    case 14: return "unavailable";
    case 15: return "data loss";
    case 16: return "unauthenticated";
    default: break;
    }
    // 0 reaching here means ok=false for a non-status reason (e.g. the response
    // body failed to parse) — "failed" is all we can honestly say.
    return code == 0 ? "failed" : "status " + std::to_string(code);
}

namespace {

/// The message to show for a failed RPC: the server's own grpc-message when it
/// sent one, else the status code by name.
std::string failure_text(const GnmiResult& r) {
    return r.grpc_message.empty() ? grpc_status_text(r.grpc_status)
                                  : r.grpc_message;
}

} // namespace

std::string format_get(const GnmiResult& r) {
    if (!r.ok) return clamp_sms("ERR GNMI GET " + failure_text(r));
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
    if (!r.ok) return clamp_sms("ERR GNMI SET " + failure_text(r));
    return clamp_sms("OK GNMI SET " + std::to_string(n) + " path(s) updated");
}

} // namespace zerotouch
