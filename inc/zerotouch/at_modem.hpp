#ifndef __zerotouch_at_modem_hpp__
#define __zerotouch_at_modem_hpp__

#include <cstdint>
#include <string>

#include <ace/Event_Handler.h>

#include "zerotouch/modem.hpp"

#include "sms_receiver.hpp"   // cellular::SmsReassembler (reused)

/**
 * @file at_modem.hpp
 * @brief Generic 3GPP (27.005/27.007) AT modem — the device-facing IModem.
 *
 * Single-threaded and reactor-driven: a synchronous AT engine (write command,
 * read lines until OK/ERROR) plus an ACE timer that polls SIM/ME storage for
 * inbound SMS (AT+CMGL, PDU mode). PDU encode/decode is reused verbatim from the
 * iot wan/cellular module (cellular::encode_sms_submit / decode_sms_deliver);
 * concatenated messages are reassembled with cellular::SmsReassembler. URCs are
 * disabled (AT+CNMI=0…) so the poll is the only reader — no reactor/at()
 * interleaving.
 *
 * Needs a real serial port + ACE, so it builds only in the device/Docker
 * toolchain (ZT_BUILD_STANDALONE) and is validated on hardware. The pure command
 * path is exercised on the host through MockModem. See DESIGN.md.
 */

namespace zerotouch {

class AtModem : public IModem, public ACE_Event_Handler {
public:
    struct Config {
        std::string   dev      = "/dev/ttyUSB2"; ///< AT channel
        std::uint32_t baud     = 115200;
        std::uint32_t poll_sec = 5;              ///< inbound-SMS poll interval
    };

    explicit AtModem(Config cfg) : m_cfg(std::move(cfg)) {}
    ~AtModem() override;

    // ── IModem ──────────────────────────────────────────────────────────────
    AtResult at(const std::string& cmd) override;
    bool send_sms(const std::string& to, const std::string& text) override;
    void on_sms(SmsFn cb) override { m_on_sms = std::move(cb); }
    void start() override;

    // ── ACE_Event_Handler ───────────────────────────────────────────────────
    int handle_timeout(const ACE_Time_Value&, const void*) override;

private:
    bool        open_port();
    void        configure();
    void        drain_sms();
    bool        write_all(const std::string& s);
    /// Read complete lines into out until a terminal token or timeout; returns
    /// on OK/ERROR. `want_prompt` returns as soon as a ">" is seen (CMGS).
    AtResult    run_at(const std::string& cmd, int timeout_ms, bool want_prompt = false);

    Config                    m_cfg;
    int                       m_fd = -1;
    SmsFn                     m_on_sms;
    cellular::SmsReassembler  m_reasm;
    std::string               m_rx;   ///< partial-line buffer across reads
};

} // namespace zerotouch

#endif /* __zerotouch_at_modem_hpp__ */
