#ifndef __zerotouch_direct_action_sink_hpp__
#define __zerotouch_direct_action_sink_hpp__

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "smsctl/executor.hpp"   // smsctl::DsSink
#include "zerotouch/modem.hpp"

/**
 * @file direct_action_sink.hpp
 * @brief The standalone backend for the classic smsctl commands: instead of
 *        writing ds keys for other iot daemons to act on, it performs the action
 *        directly — AT commands on the shared modem, and a reboot syscall.
 *
 * It reuses smsctl::Executor unchanged by satisfying its DsSink seam and mapping
 * the specific keys/triggers the classic commands use:
 *   set("cell.apn", apn)          -> AT+CGDCONT=1,"IP","<apn>"
 *   set("cell.reset.request", …)  -> AT+CFUN=0 ; AT+CFUN=1   (radio cycle)
 *   get("cell.reg")               -> AT+CREG?                (registration)
 *   get("cell.signal.dbm")        -> AT+CSQ                  (RSSI -> dBm)
 *   get("cell.ip")                -> AT+CGPADDR=1            (bearer IP)
 *   arm_trigger(reboot.request)   -> reboot syscall
 * Everything else reads as absent ("-") / writes as a no-op. WIFI / FACTORY-RESET
 * are rejected earlier by the daemon, so their keys never reach here.
 */

namespace zerotouch {

class DirectActionSink : public smsctl::DsSink {
public:
    /// Perform the actual system reboot. Injected so tests don't reboot the box.
    using RebootFn = std::function<bool()>;

    explicit DirectActionSink(IModem& modem, RebootFn reboot = {})
      : m_modem(modem), m_reboot(std::move(reboot)) {}

    bool set(const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& key) override;
    bool arm_trigger(const std::string& path, const std::string& content) override;
    std::uint64_t now_ms() override;

private:
    IModem&  m_modem;
    RebootFn m_reboot;
};

} // namespace zerotouch

#endif /* __zerotouch_direct_action_sink_hpp__ */
