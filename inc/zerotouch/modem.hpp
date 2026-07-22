#ifndef __zerotouch_modem_hpp__
#define __zerotouch_modem_hpp__

#include <functional>
#include <string>
#include <vector>

#include "zerotouch/sms_transport.hpp"  // InboundSms

/**
 * @file modem.hpp
 * @brief The "any modem" seam for the standalone appliance: one AT channel that
 *        both AtModemTransport (SMS) and DirectActionSink (APN/CFUN/status) share,
 *        so the API stays constant while the concrete modem model varies.
 *
 * Pure interface — the generic 3GPP (27.005/27.007) AT implementation is the
 * device-facing piece (later phase); Phase 1 uses a mock. See DESIGN.md.
 */

namespace zerotouch {

/// Result of one AT command exchange.
struct AtResult {
    bool                     ok = false;   ///< terminal `OK` (true) vs ERROR/timeout
    std::vector<std::string> lines;        ///< informational lines, e.g. "+CSQ: 17,99"

    /// First response line beginning with `prefix` (e.g. "+CREG:"), or "" if none.
    std::string line_with(const std::string& prefix) const {
        for (const auto& l : lines)
            if (l.rfind(prefix, 0) == 0) return l;
        return {};
    }
};

/// A modem exposing an AT channel + SMS. Implementations serialise all traffic
/// over the single physical channel, so SMS and command AT never interleave.
struct IModem {
    using SmsFn = std::function<void(const InboundSms&)>;

    virtual ~IModem() = default;

    /// Issue an AT command and wait for the terminal OK/ERROR.
    virtual AtResult at(const std::string& cmd) = 0;

    /// Send a mobile-originated SMS (the impl handles PDU + concatenation).
    virtual bool send_sms(const std::string& to, const std::string& text) = 0;

    /// Register the inbound MT-SMS callback (used by AtModemTransport, Phase 2).
    virtual void on_sms(SmsFn cb) = 0;

    /// Open the port, configure the modem, and join the run loop.
    virtual void start() = 0;
};

} // namespace zerotouch

#endif /* __zerotouch_modem_hpp__ */
