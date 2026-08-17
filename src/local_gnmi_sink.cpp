#include "zerotouch/local_gnmi_sink.hpp"

#include "zerotouch/path_policy.hpp"

// grace-server (submodule) — the reused libevent gNMI client + helpers.
#include "gnmi_client.hpp"
#include "gnmi_util.hpp"
#include "tls_config.hpp"

#include "gnmi/gnmi.pb.h"

/**
 * @file local_gnmi_sink.cpp
 * @brief LocalGnmiSink: GnmiSink → gnmi_client::call() against 127.0.0.1.
 *        Compiled only when ZT_BUILD_GNMI is ON. See local_gnmi_sink.hpp.
 */

namespace zerotouch {

namespace {

/// Map a gnmi_client transport/status outcome onto a GnmiResult shell. Returns
/// true when the RPC succeeded (status 0) and the body is worth decoding.
///
/// The status is reported as-is and `grpc_message` carries only what the server
/// actually sent — we do NOT synthesise a placeholder here. Most gNMI servers
/// (grace-server's grpc_session included) send `grpc-status` with no
/// `grpc-message`, and naming that code is the reply layer's job: see
/// grpc_status_text(), which turns 7 into "permission denied" rather than the
/// "status 7" a placeholder here would lock in.
bool rpc_ok(const gnmi_client::response& r, GnmiResult& out) {
    out.grpc_status  = r.grpc_status;
    out.grpc_message = r.grpc_message;
    return r.grpc_status == 0;
}

} // namespace

GnmiResult LocalGnmiSink::get(const std::vector<std::string>& xpaths) {
    GnmiResult out;
    const auto& deny =
        m_cfg.deny_tokens.empty() ? default_deny_tokens() : m_cfg.deny_tokens;

    gnmi::GetRequest req;
    req.mutable_prefix()->set_target(m_cfg.get_role);
    req.set_encoding(gnmi::JSON);

    // Pre-filter: a denylisted xpath is never sent to the server. Record its
    // error row now; keep the order of the request for later correlation.
    std::vector<std::string> asked;
    for (const auto& xp : xpaths) {
        if (is_sensitive_path(xp, deny)) {
            out.paths.push_back({xp, "", "sensitive path denied"});
            continue;
        }
        *req.add_path() = gnmi_util::parse_yang_path(xp);
        asked.push_back(xp);
    }

    // Nothing to fetch (all denied / empty) — report what we have.
    if (asked.empty()) {
        out.ok = out.paths.empty() ? false : true;   // denied-only is "handled"
        out.grpc_status = 0;
        return out;
    }

    std::string pb;
    req.SerializeToString(&pb);

    const auto r = gnmi_client::call(m_cfg.host, m_cfg.port, "/gnmi.gNMI/Get",
                                     pb, tls_config{});
    if (!rpc_ok(r, out)) return out;   // ok stays false

    gnmi::GetResponse resp;
    if (!resp.ParseFromString(r.body_pb)) {
        out.grpc_message = "response parse failed";
        return out;
    }

    // Flatten notifications → one row per returned leaf, re-checking the
    // denylist on the way OUT.
    //
    // Pre-filtering the request is not sufficient: a gNMI Get on a container
    // path returns the whole subtree, so `GET /system` (or `/`) asks for a path
    // that matches no deny token yet comes back carrying every leaf beneath it —
    // passwords included. Screening only the requested xpath would let exactly
    // the secrets this denylist exists to stop ride out over plaintext SMS.
    for (const auto& notif : resp.notification()) {
        for (const auto& u : notif.update()) {
            const std::string xp = gnmi_util::path_to_string(u.path());
            if (is_sensitive_path(xp, deny)) {
                out.paths.push_back({xp, "", "sensitive path denied"});
                continue;
            }
            out.paths.push_back({xp, gnmi_util::typed_value_to_string(u.val()), ""});
        }
    }

    // The RPC succeeded, so ok=true: any per-path error rows (e.g. the denied
    // paths added above) are rendered by format_get's OK branch as `<error>`,
    // which is exactly how a sensitive value stays masked. Flipping ok=false
    // here would instead collapse the whole reply to "ERR GNMI GET" and hide
    // the successfully-read leaves.
    out.ok = true;
    return out;
}

GnmiResult LocalGnmiSink::set(
    const std::vector<std::pair<std::string, std::string>>& updates) {
    GnmiResult out;

    gnmi::SetRequest req;
    req.mutable_prefix()->set_target(m_cfg.set_role);
    for (const auto& [xpath, value] : updates) {
        auto* upd = req.add_update();
        *upd->mutable_path() = gnmi_util::parse_yang_path(xpath);
        gnmi_util::set_typed_value(upd->mutable_val(), value);
    }

    std::string pb;
    req.SerializeToString(&pb);

    const auto r = gnmi_client::call(m_cfg.host, m_cfg.port, "/gnmi.gNMI/Set",
                                     pb, tls_config{});
    if (!rpc_ok(r, out)) return out;

    gnmi::SetResponse resp;
    if (!resp.ParseFromString(r.body_pb)) {
        out.grpc_message = "response parse failed";
        return out;
    }

    for (const auto& ur : resp.response())
        out.paths.push_back({gnmi_util::path_to_string(ur.path()), "", ""});

    out.ok = true;
    return out;
}

} // namespace zerotouch
