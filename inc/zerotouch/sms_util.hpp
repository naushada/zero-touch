#ifndef __zerotouch_sms_util_hpp__
#define __zerotouch_sms_util_hpp__

#include <string>

/**
 * @file sms_util.hpp
 * @brief Pure SMS-transport helpers, host-testable without ACE or ds.
 */

namespace zerotouch {

/// True when `sender` looks like an E.164 number we can actually reply to.
/// An alphanumeric sender ID ("AZ-AIRTEL-S") cannot receive SMS, so replying
/// would silently burn credit forever — DsSmsTransport::send drops those.
/// Accepts '+', spaces and '-' as separators; requires at least 7 digits.
bool is_reachable_sender(const std::string& sender);

} // namespace zerotouch

#endif /* __zerotouch_sms_util_hpp__ */
