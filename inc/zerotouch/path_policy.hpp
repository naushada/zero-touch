#ifndef __zerotouch_path_policy_hpp__
#define __zerotouch_path_policy_hpp__

#include <string>
#include <vector>

/**
 * @file path_policy.hpp
 * @brief Sensitive-path denylist for gNMI GET. Pure and host-testable so it is
 *        covered by the default suite even though LocalGnmiSink (which uses it)
 *        needs protobuf/libevent to build.
 *
 * A GET reply travels over plaintext SMS, so even an Admin-gated read must not
 * be allowed to return a credential. LocalGnmiSink drops any requested xpath
 * that matches the denylist before it ever reaches the gNMI server, and the
 * reply carries an error marker instead of a value. See DESIGN.md.
 */

namespace zerotouch {

/// Default deny tokens: an xpath is sensitive if (case-insensitively) it
/// contains any of these substrings.
const std::vector<std::string>& default_deny_tokens();

/// True when `xpath` matches any token in `tokens` (case-insensitive substring).
bool is_sensitive_path(const std::string& xpath,
                       const std::vector<std::string>& tokens);

/// Convenience overload using default_deny_tokens().
bool is_sensitive_path(const std::string& xpath);

} // namespace zerotouch

#endif /* __zerotouch_path_policy_hpp__ */
