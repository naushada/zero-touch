#ifndef __zerotouch_gnmi_command_hpp__
#define __zerotouch_gnmi_command_hpp__

#include <string>
#include <utility>
#include <vector>

/**
 * @file gnmi_command.hpp
 * @brief Parser for the zero-touch gnmi command layer, added on top of
 *        smsctl's grammar WITHOUT modifying smsctl.
 *
 * Grammar (tokenised with smsctl::tokenize so quoting/escapes are shared):
 *     IOT GNMI GET <xpath[,xpath...]>
 *     IOT GNMI SET <xpath[,xpath...]> <value[,value...]>
 *
 * Pure and host-testable. See DESIGN.md.
 */

namespace zerotouch {

enum class GnmiKind {
    NotGnmi,   ///< not an `IOT GNMI …` message — hand back to the smsctl engine
    Unknown,   ///< `IOT GNMI <junk>` — meant a gnmi command but malformed
    Get,       ///< xpaths populated
    Set,       ///< updates populated (xpath/value pairs)
};

struct GnmiCommand {
    GnmiKind                                         kind = GnmiKind::NotGnmi;
    std::vector<std::string>                         xpaths;   ///< Get
    std::vector<std::pair<std::string, std::string>> updates;  ///< Set
    std::string                                      error;    ///< set for Unknown
};

/// Parse an already-tokenised SMS body. Returns NotGnmi unless the tokens begin
/// (case-insensitively) with `IOT GNMI`. Splits xpath/value CSV lists and, for
/// Set, verifies the two lists have equal length (else Unknown with an error).
GnmiCommand parse_gnmi(const std::vector<std::string>& tokens);

} // namespace zerotouch

#endif /* __zerotouch_gnmi_command_hpp__ */
