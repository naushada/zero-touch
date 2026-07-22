#include "zerotouch/ds_sms_transport.hpp"

#include <ace/Log_Msg.h>
#include <ace/OS_NS_sys_time.h>
#include <ace/Reactor.h>

#include "zerotouch/sms_util.hpp"

/**
 * @file ds_sms_transport.cpp
 * @brief MT/MO SMS over the cellular-client ds envelope. Built when ZT_BUILD_DS
 *        is ON. See ds_sms_transport.hpp.
 */

namespace zerotouch {

namespace {

std::uint64_t now_ms() {
    const ACE_Time_Value tv = ACE_OS::gettimeofday();
    return static_cast<std::uint64_t>(tv.sec()) * 1000ULL +
           static_cast<std::uint64_t>(tv.usec() / 1000);
}

/// Read the three sms.last.* keys in one round trip.
bool read_last(data_store::Client& ds, std::string& sender, std::string& text,
               std::string& ts) {
    std::vector<data_store::Client::GetResult> got;
    if (!ds.get({std::string("sms.last.sender"), std::string("sms.last.text"),
                 std::string("sms.last.ts")}, got).ok || got.size() < 3)
        return false;
    auto val = [&](std::size_t i) -> std::string {
        if (!got[i].has_value) return {};
        return data_store::to_string(got[i].value).value_or(std::string());
    };
    sender = val(0);
    text   = val(1);
    ts     = val(2);
    return true;
}

} // namespace

ACE_Reactor* DsSmsTransport::reactor() const {
    return m_reactor ? m_reactor : ACE_Reactor::instance();
}

void DsSmsTransport::start() {
    // Replay guard: whatever is in sms.last.* now predates us (it may have been
    // drained from the SIM store at boot). Record it as already seen so a
    // restart never re-delivers an old command.
    {
        std::string sender, text, ts;
        if (read_last(m_ds, sender, text, ts)) {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_seen_sender = sender;
            m_seen_text   = text;
            m_seen_ts     = ts;
        }
    }

    // Watch the sms.* bump counter. The callback runs on the ds LISTENER thread,
    // so it only flags dirty + wakes the reactor; the drain is on the reactor
    // thread. Watching sms.version (not sms.last.text) also catches a repeat of
    // the identical text from the same sender.
    m_ds.watch(std::string("sms.version"),
               [this](const data_store::Client::Event&) {
                   {
                       std::lock_guard<std::mutex> lk(m_mtx);
                       m_sms_dirty = true;
                   }
                   reactor()->notify(this);
               });
}

int DsSmsTransport::handle_exception(ACE_HANDLE) {
    bool dirty;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        dirty = m_sms_dirty;
        m_sms_dirty = false;
    }
    if (dirty) drain_inbound();
    return 0;
}

void DsSmsTransport::drain_inbound() {
    std::string sender, text, ts;
    if (!read_last(m_ds, sender, text, ts)) return;
    if (sender.empty() || text.empty()) return;

    {   // Dedupe: the same (sender, text, ts) is delivered exactly once. This is
        // also the replay guard, baselined in start().
        std::lock_guard<std::mutex> lk(m_mtx);
        if (sender == m_seen_sender && text == m_seen_text && ts == m_seen_ts)
            return;
        m_seen_sender = sender;
        m_seen_text   = text;
        m_seen_ts     = ts;
    }

    if (m_on_message) m_on_message(InboundSms{sender, text, ts});
}

bool DsSmsTransport::send(const std::string& to, const std::string& text) {
    // An alphanumeric sender ID cannot receive SMS — dropping avoids burning
    // credit forever. The engine still processed the command; only the reply is
    // suppressed.
    if (!is_reachable_sender(to)) {
        ACE_DEBUG((LM_INFO,
                   ACE_TEXT("%D [zerotouch] sender %C is not E.164 — no reply\n"),
                   to.c_str()));
        return false;
    }

    // Reply over the proven MO envelope: set the payload, then bump the request
    // token that cellular-client watches.
    std::vector<data_store::KV> out;
    out.emplace_back("sms.send.to",   data_store::Value{to});
    out.emplace_back("sms.send.text", data_store::Value{text});
    if (!m_ds.set(out).ok ||
        !m_ds.set(std::string("sms.send.request"),
                  data_store::Value{std::to_string(now_ms())}).ok) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [zerotouch] reply send failed\n")));
        return false;
    }
    return true;
}

} // namespace zerotouch
