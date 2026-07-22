#include "zerotouch/bridge.hpp"

#include "zerotouch/gnmi_command.hpp"

/**
 * @file bridge.cpp
 * @brief SMS ↔ gnmi wiring. Pure apart from the injected transport. See bridge.hpp.
 */

namespace zerotouch {

void Bridge::start() {
    m_tx.on_message([this](const InboundSms& in) { on_sms(in); });
    m_tx.start();
}

void Bridge::on_sms(const InboundSms& in) {
    // Allowlist / enabled gate: a disallowed sender is dropped in silence,
    // before we tokenise or reply, for both gnmi and classic commands.
    if (m_allow && !m_allow(in.sender)) return;

    const auto tokens = m_tok ? m_tok(in.text) : std::vector<std::string>{};
    const GnmiCommand g = parse_gnmi(tokens);

    if (g.kind != GnmiKind::NotGnmi) {
        const std::string reply = m_gex.handle(g, in.sender);
        if (!reply.empty()) m_tx.send(in.sender, reply);
        return;
    }

    // Not a gnmi command — hand to the classic smsctl engine if wired.
    if (m_fallback) {
        const std::string reply = m_fallback(in.sender, in.text);
        if (!reply.empty()) m_tx.send(in.sender, reply);
    }
    // else: drop silently (no fallback configured).
}

} // namespace zerotouch
