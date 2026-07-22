#ifndef __zerotouch_local_gnmi_sink_hpp__
#define __zerotouch_local_gnmi_sink_hpp__

#include <cstdint>
#include <string>
#include <vector>

#include "zerotouch/gnmi_sink.hpp"

/**
 * @file local_gnmi_sink.hpp
 * @brief GnmiSink backed by grace-server's gnmi_client, targeting the
 *        device-local gNMI server (127.0.0.1) over a blocking unary RPC.
 *
 * Builds gnmi::GetRequest / SetRequest with gnmi_util, calls
 * gnmi_client::call(), and decodes the response into GnmiResult. A GET xpath on
 * the sensitive-path denylist is never sent — its row reports an error instead
 * of a value, so a secret cannot leak over plaintext SMS. SET is Admin-gated at
 * the command layer and is not denylisted (writing config is not exfil).
 *
 * This translation unit depends on protobuf + libevent + the grace-server
 * framework, so it is compiled only when ZT_BUILD_GNMI is ON. The pure command
 * layer (and its tests) never include this header. See DESIGN.md.
 */

namespace zerotouch {

class LocalGnmiSink : public GnmiSink {
public:
    struct Config {
        std::string   host = "127.0.0.1";  ///< device-local target; never from SMS
        std::uint16_t port = 50051;        ///< bound to the on-device gNMI server
        std::string   get_role = "VIEWER"; ///< prefix.target for GetRequest RBAC
        std::string   set_role = "ADMIN";  ///< prefix.target for SetRequest RBAC
        std::vector<std::string> deny_tokens;  ///< empty → default_deny_tokens()
    };

    explicit LocalGnmiSink(Config cfg = {}) : m_cfg(std::move(cfg)) {}

    GnmiResult get(const std::vector<std::string>& xpaths) override;
    GnmiResult set(
        const std::vector<std::pair<std::string, std::string>>& updates) override;

private:
    Config m_cfg;
};

} // namespace zerotouch

#endif /* __zerotouch_local_gnmi_sink_hpp__ */
