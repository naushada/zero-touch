#ifndef __zerotouch_config_hpp__
#define __zerotouch_config_hpp__

#include <cstdint>
#include <string>
#include <vector>

/**
 * @file config.hpp
 * @brief Standalone-appliance configuration, parsed from a simple `key = value`
 *        file (the ds config plane's replacement when there is no ds-server).
 *
 * Pure and host-testable: `parse_config` takes the file *contents*, not a path.
 * Format: one `key = value` per line; `#` starts a comment; blank lines and
 * unknown keys are ignored, so an old/newer file never fails the daemon.
 */

namespace zerotouch {

/// Modem family selection. `Auto` detects the vendor at runtime (AT+GMI/CGMM →
/// cellular::parse_vendor); the rest force it. The AT command set is ~standard
/// 3GPP for SMS, so this mainly future-proofs vendor-specific quirks and drives
/// logging — WP7702 is Sierra.
enum class ModemType { Auto, Sierra, Quectel, UBlox, Generic };

/// Parse a `modem.type` value (case-insensitive); unknown/empty → Auto.
ModemType   parse_modem_type(const std::string& s);
const char* modem_type_name(ModemType t);

struct AppConfig {
    bool          enabled          = false;        ///< inert until true
    std::string   gnmi_host        = "127.0.0.1";  ///< device-local gNMI target
    std::uint16_t gnmi_port        = 50051;
    std::vector<std::string> allowed_numbers;      ///< empty = any sender may login
    std::uint32_t session_ttl_sec  = 600;
    std::uint32_t lockout_failures = 5;
    std::uint32_t lockout_sec      = 900;
    std::string   modem_dev        = "/dev/ttyUSB2"; ///< AT channel
    std::uint32_t modem_baud       = 115200;
    ModemType     modem_type       = ModemType::Auto;
    std::string   users_file       = "/etc/zerotouch/users";
};

/// Recognised keys: enabled, gnmi.host, gnmi.port, allowed.numbers (CSV),
/// session.ttl.sec, lockout.failures, lockout.sec, modem.dev, modem.baud,
/// modem.type, users.file. Absent keys keep their default.
AppConfig parse_config(const std::string& text);

} // namespace zerotouch

#endif /* __zerotouch_config_hpp__ */
