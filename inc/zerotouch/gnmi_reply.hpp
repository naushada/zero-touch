#ifndef __zerotouch_gnmi_reply_hpp__
#define __zerotouch_gnmi_reply_hpp__

#include <cstddef>
#include <string>

#include "zerotouch/gnmi_sink.hpp"

/**
 * @file gnmi_reply.hpp
 * @brief Format a GnmiResult into a single reply SMS. Pure; never echoes a
 *        value carrying a per-path error. See DESIGN.md.
 */

namespace zerotouch {

/// One GSM-7 SMS.
constexpr std::size_t kMaxReply = 160;

/// `OK GNMI GET /p=val; /p2=val2` or `ERR GNMI GET <grpc-message>`.
std::string format_get(const GnmiResult& r);

/// `OK GNMI SET n path(s) updated` or `ERR GNMI SET <grpc-message>`.
std::string format_set(const GnmiResult& r, std::size_t n);

/// Clamp to one SMS with a trailing ellipsis when truncated.
std::string clamp_sms(std::string s, std::size_t max = kMaxReply);

} // namespace zerotouch

#endif /* __zerotouch_gnmi_reply_hpp__ */
