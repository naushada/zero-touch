#ifndef __zerotouch_zerotouchd_hpp__
#define __zerotouch_zerotouchd_hpp__

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <ace/Event_Handler.h>

#include "data_store/client.hpp"

#include "smsctl/session.hpp"

#include "zerotouch/bridge.hpp"
#include "zerotouch/ds_sms_transport.hpp"
#include "zerotouch/gnmi_executor.hpp"
#include "zerotouch/local_gnmi_sink.hpp"

/**
 * @file zerotouchd.hpp
 * @brief The zero-touchd daemon: authenticated device control + gNMI over SMS.
 *
 * Composes the two seams over ONE data_store::Client and the ACE reactor:
 *   - DsSmsTransport delivers inbound SMS and sends replies (its own sms.version
 *     watch + reactor notify);
 *   - the Bridge tokenises with smsctl::tokenize, routes `IOT GNMI …` to the
 *     GnmiExecutor (LocalGnmiSink → device-local gNMI) and everything else to
 *     the classic smsctl::Executor — both over the SAME SessionStore, so one
 *     `IOT LOGIN` authorises gnmi and classic commands alike.
 *
 * This daemon holds no extra privilege: classic mutations are ds writes (ds
 * ACLs apply) or trigger files under /run/iot for the root .path units. Ships
 * disabled (zerotouch.enabled=false). See DESIGN.md.
 */

namespace zerotouch {

class ZeroTouchClient : public ACE_Event_Handler {
public:
    struct Config {
        std::string ds_sock;   ///< "" → ds default socket
    };

    explicit ZeroTouchClient(Config cfg);
    ~ZeroTouchClient() override = default;

    int run();

    /// 1s sweep: expire sessions + factory-reset nonces.
    int handle_timeout(const ACE_Time_Value&, const void*) override;
    /// Reactor wake-up from the config watch (a zerotouch.* key changed).
    int handle_exception(ACE_HANDLE = ACE_INVALID_HANDLE) override;

private:
    void load_config_from_ds();
    void publish_state();
    /// Resolve a user from auth.users.admin.* / auth.users.accounts (mirrors
    /// iot-httpd, so SMS login accepts the very same passwords as the device-ui).
    bool lookup_account(const std::string& id, smsctl::Account& out);

    // Bridge callbacks ────────────────────────────────────────────────────────
    /// AuthFn: the live session's access for `sender` (None when not logged in).
    Access sender_access(const std::string& sender);
    /// FallbackFn: run a non-gnmi `IOT …` command through smsctl::Executor.
    std::string classic_reply(const std::string& sender, const std::string& text);
    /// AllowFn: enabled AND sender on the allowlist (else silent drop).
    bool sender_allowed(const std::string& sender);

    Config                m_cfg;
    data_store::Client    m_ds;
    smsctl::SessionStore  m_sessions;
    LocalGnmiSink         m_sink;       ///< device-local gNMI backend
    GnmiExecutor          m_gex;        ///< gnmi command layer over m_sink
    DsSmsTransport        m_transport;  ///< SMS in/out over the ds envelope
    std::unique_ptr<Bridge> m_bridge;   ///< built in run(), after config load

    bool          m_enabled   = false;
    std::uint16_t m_gnmi_port = 50051;

    std::mutex        m_mtx;
    bool              m_cfg_dirty = false;
    std::uint64_t     m_handled   = 0;   ///< commands executed (→ telemetry)
};

} // namespace zerotouch

#endif /* __zerotouch_zerotouchd_hpp__ */
