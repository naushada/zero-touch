#include "zerotouch/gnmi_executor.hpp"

#include "zerotouch/gnmi_reply.hpp"

/**
 * @file gnmi_executor.cpp
 * @brief Auth + dispatch for gnmi commands. Pure; see gnmi_executor.hpp.
 */

namespace zerotouch {

std::string GnmiExecutor::handle(const GnmiCommand& cmd,
                                 const std::string& sender) {
    const Access access = m_auth ? m_auth(sender) : Access::None;

    switch (cmd.kind) {
    case GnmiKind::NotGnmi:
        // Caller should not route a NotGnmi command here; be defensive.
        return {};

    case GnmiKind::Unknown:
        return clamp_sms("ERR " + cmd.error);

    case GnmiKind::Get:
        if (access == Access::None)
            return clamp_sms("ERR GNMI GET login required");
        return format_get(m_sink.get(cmd.xpaths));

    case GnmiKind::Set:
        if (access != Access::Admin)
            return clamp_sms("ERR GNMI SET admin login required");
        return format_set(m_sink.set(cmd.updates), cmd.updates.size());
    }
    return {};
}

} // namespace zerotouch
