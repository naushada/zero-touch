#ifndef __zerotouch_gnmi_executor_hpp__
#define __zerotouch_gnmi_executor_hpp__

#include <functional>
#include <string>

#include "zerotouch/gnmi_command.hpp"
#include "zerotouch/gnmi_sink.hpp"

/**
 * @file gnmi_executor.hpp
 * @brief Authenticate a parsed gnmi command and run it through a GnmiSink,
 *        returning the reply SMS text.
 *
 * Auth is injected as an AuthFn rather than a hard dependency on smsctl's
 * SessionStore, so the core stays independent and host-testable. The daemon
 * (Phase 6) binds the AuthFn to the SAME smsctl::SessionStore the classic
 * commands use, giving one login for both. See DESIGN.md.
 */

namespace zerotouch {

/// Access level the sender currently holds (resolved from the shared session).
enum class Access { None, Viewer, Admin };

/// Resolve a sender's current access. Injected so the engine is ds/session-free.
using AuthFn = std::function<Access(const std::string& sender)>;

class GnmiExecutor {
public:
    GnmiExecutor(GnmiSink& sink, AuthFn auth)
      : m_sink(sink), m_auth(std::move(auth)) {}

    /// Authenticate + execute. Returns the reply SMS body (`OK …` / `ERR …`),
    /// clamped to one SMS. GET needs Viewer; SET needs Admin. Never echoes a
    /// value for a denylisted path (the sink strips it).
    std::string handle(const GnmiCommand& cmd, const std::string& sender);

private:
    GnmiSink& m_sink;
    AuthFn    m_auth;
};

} // namespace zerotouch

#endif /* __zerotouch_gnmi_executor_hpp__ */
