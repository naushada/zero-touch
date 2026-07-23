#ifndef __zerotouch_at_modem_transport_hpp__
#define __zerotouch_at_modem_transport_hpp__

#include <string>
#include <utility>

#include "zerotouch/modem.hpp"
#include "zerotouch/sms_transport.hpp"

/**
 * @file at_modem_transport.hpp
 * @brief ISmsTransport impl for the standalone appliance: SMS straight over the
 *        modem's AT channel, no ds / cellular-client.
 *
 * Deliberately thin — all the SMS mechanics (PDU codec, URC/poll, storage
 * delete) live in the IModem implementation, so this is a pure adapter that
 * maps IModem's SMS surface onto the transport seam. Header-only and
 * host-testable against MockModem.
 */

namespace zerotouch {

class AtModemTransport : public ISmsTransport {
public:
    explicit AtModemTransport(IModem& modem) : m_modem(modem) {}

    void on_message(MessageFn cb) override { m_modem.on_sms(std::move(cb)); }
    bool send(const std::string& to, const std::string& text) override {
        return m_modem.send_sms(to, text);
    }
    void start() override { m_modem.start(); }

private:
    IModem& m_modem;
};

} // namespace zerotouch

#endif /* __zerotouch_at_modem_transport_hpp__ */
