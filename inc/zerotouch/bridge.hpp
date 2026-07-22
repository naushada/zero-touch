#ifndef __zerotouch_bridge_hpp__
#define __zerotouch_bridge_hpp__

#include <functional>
#include <string>
#include <vector>

#include "zerotouch/gnmi_executor.hpp"
#include "zerotouch/sms_transport.hpp"

/**
 * @file bridge.hpp
 * @brief Wires an ISmsTransport to the gnmi command layer: for each inbound
 *        SMS, tokenise → parse_gnmi → (gnmi) execute or (else) fall back to the
 *        classic smsctl engine, then reply.
 *
 * The tokenizer and the classic-command fallback are injected, so the bridge is
 * host-testable with mocks and stays free of a compile-time smsctl dependency.
 * The daemon (Phase 6) binds `tokenize` to smsctl::tokenize and `fallback` to
 * an smsctl::Executor over the SAME SessionStore the AuthFn reads. See DESIGN.md.
 */

namespace zerotouch {

/// Split an SMS body into tokens (daemon binds smsctl::tokenize).
using TokenizeFn = std::function<std::vector<std::string>(const std::string&)>;

/// Handle a non-gnmi `IOT …` command. Returns the reply, or "" to drop
/// silently (the smsctl contract for NotACommand / disallowed senders).
using FallbackFn =
    std::function<std::string(const std::string& sender, const std::string& text)>;

/// Gate a sender before any processing. False → drop the message in SILENCE (no
/// reply, no execution), so the device is not an oracle and carrier spam costs
/// nothing. The daemon binds this to "enabled AND sender on the allowlist".
using AllowFn = std::function<bool(const std::string& sender)>;

class Bridge {
public:
    Bridge(ISmsTransport& tx, GnmiExecutor& gex, TokenizeFn tokenize,
           FallbackFn fallback = {}, AllowFn allow = {})
      : m_tx(tx), m_gex(gex), m_tok(std::move(tokenize)),
        m_fallback(std::move(fallback)), m_allow(std::move(allow)) {}

    /// Register the inbound handler and start the transport.
    void start();

    /// Process one inbound SMS (exposed for tests). Sends a reply unless the
    /// message is dropped silently.
    void on_sms(const InboundSms& in);

private:
    ISmsTransport& m_tx;
    GnmiExecutor&  m_gex;
    TokenizeFn     m_tok;
    FallbackFn     m_fallback;
    AllowFn        m_allow;
};

} // namespace zerotouch

#endif /* __zerotouch_bridge_hpp__ */
