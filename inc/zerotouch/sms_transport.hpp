#ifndef __zerotouch_sms_transport_hpp__
#define __zerotouch_sms_transport_hpp__

#include <functional>
#include <string>

/**
 * @file sms_transport.hpp
 * @brief The "any modem" seam: a stable SMS in/out interface whose concrete
 *        model (ds/cellular route, direct AT modem, SMPP, cloud API) can change
 *        without touching the command engine.
 *
 * The bridge only ever sees ISmsTransport. DsSmsTransport is implementation #1
 * (the existing sms.last.* / sms.send.* ds route); MockTransport backs the
 * host tests. See DESIGN.md.
 */

namespace zerotouch {

/// One inbound mobile-terminated SMS, already reassembled from its parts.
struct InboundSms {
    std::string sender;   ///< E.164 MSISDN of the originator (spoofable — see DESIGN.md)
    std::string text;     ///< the message body
    std::string ts;       ///< service-centre timestamp / arrival token (replay guard)
};

/// Modem-agnostic SMS transport. All implementations are equivalent to the
/// bridge; only the underlying model differs.
struct ISmsTransport {
    using MessageFn = std::function<void(const InboundSms&)>;

    virtual ~ISmsTransport() = default;

    /// Register the callback invoked for each inbound MT SMS. Must be set before
    /// start(). Implementations deliver on the reactor/run-loop thread.
    virtual void on_message(MessageFn cb) = 0;

    /// Send a mobile-originated reply. Returns false if it could not be queued
    /// (transport down, ACL). Text is expected to be one clamped GSM-7 SMS.
    virtual bool send(const std::string& to, const std::string& text) = 0;

    /// Begin delivering messages (open the modem / join the reactor). Returns
    /// only when the transport is wired; message delivery is asynchronous.
    virtual void start() = 0;
};

} // namespace zerotouch

#endif /* __zerotouch_sms_transport_hpp__ */
