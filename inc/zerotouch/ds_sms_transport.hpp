#ifndef __zerotouch_ds_sms_transport_hpp__
#define __zerotouch_ds_sms_transport_hpp__

#include <mutex>
#include <string>

#include <ace/Event_Handler.h>

#include "data_store/client.hpp"

#include "zerotouch/sms_transport.hpp"

/**
 * @file ds_sms_transport.hpp
 * @brief ISmsTransport impl #1: MT/MO SMS over the ds keys that cellular-client
 *        already publishes — the existing route, refactored behind the seam.
 *
 * Inbound: watch `sms.version` (cellular-client's bump-on-change counter for the
 * sms.* domain), then read `sms.last.{sender,text,ts}`, dedupe, and deliver via
 * on_message. Outbound: write `sms.send.{to,text}` and bump `sms.send.request`.
 * It never touches the modem — cellular-client owns that.
 *
 * Threading mirrors the smsctl daemon: the ds watch fires on the ds LISTENER
 * thread, which only flags dirty + notify()s the reactor; the drain and all
 * on_message delivery happen on the reactor thread in handle_exception(). The
 * daemon owns the reactor loop and the (already-connected) Client. This TU needs
 * ACE + datastore_client, so it is built only when ZT_BUILD_DS is ON.
 * See DESIGN.md.
 */

namespace zerotouch {

class DsSmsTransport : public ISmsTransport, public ACE_Event_Handler {
public:
    /// `ds` must already be connected. `reactor` receives the wake-up notify;
    /// nullptr uses ACE_Reactor::instance().
    explicit DsSmsTransport(data_store::Client& ds, ACE_Reactor* reactor = nullptr)
      : m_ds(ds), m_reactor(reactor) {}
    ~DsSmsTransport() override = default;

    // ── ISmsTransport ───────────────────────────────────────────────────────
    void on_message(MessageFn cb) override { m_on_message = std::move(cb); }
    bool send(const std::string& to, const std::string& text) override;
    /// Baseline the replay guard from the current sms.last.*, then register the
    /// sms.version watch. Call before the reactor loop runs.
    void start() override;

    // ── ACE_Event_Handler ───────────────────────────────────────────────────
    /// Reactor wake-up from the sms.version watch: drain new inbound SMS.
    int handle_exception(ACE_HANDLE = ACE_INVALID_HANDLE) override;

private:
    void drain_inbound();
    ACE_Reactor* reactor() const;

    data_store::Client& m_ds;
    ACE_Reactor*        m_reactor;
    MessageFn           m_on_message;

    // Dirty flag set on the ds listener thread, consumed on the reactor thread;
    // m_mtx also guards the last-seen tuple (the dedupe / replay baseline).
    std::mutex  m_mtx;
    bool        m_sms_dirty = false;
    std::string m_seen_sender;
    std::string m_seen_text;
    std::string m_seen_ts;
};

} // namespace zerotouch

#endif /* __zerotouch_ds_sms_transport_hpp__ */
