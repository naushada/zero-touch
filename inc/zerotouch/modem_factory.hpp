#ifndef __zerotouch_modem_factory_hpp__
#define __zerotouch_modem_factory_hpp__

#include <memory>

#include "zerotouch/config.hpp"
#include "zerotouch/modem.hpp"

/**
 * @file modem_factory.hpp
 * @brief Config-driven modem wiring: pick and construct the concrete IModem.
 *
 * Today every family (Sierra/Quectel/u-blox/generic) is served by one
 * vendor-parameterised AtModem — the AT command set for SMS is ~standard 3GPP,
 * so the vendor mostly drives detection/logging and future quirks. This factory
 * is the seam where a genuinely different transport (a cloud SMS API, SMPP)
 * would branch to its own IModem later. Lives in the atmodem lib (needs ACE).
 */

namespace zerotouch {

/// Construct the modem the config asks for. `modem.dev` / `modem.baud` /
/// `modem.type` come from AppConfig.
std::unique_ptr<IModem> make_modem(const AppConfig& cfg);

} // namespace zerotouch

#endif /* __zerotouch_modem_factory_hpp__ */
