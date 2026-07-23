#include "zerotouch/modem_factory.hpp"

#include "zerotouch/at_modem.hpp"

/**
 * @file modem_factory.cpp
 * @brief make_modem — config → IModem. See modem_factory.hpp.
 */

namespace zerotouch {

std::unique_ptr<IModem> make_modem(const AppConfig& cfg) {
    AtModem::Config mc;
    mc.dev  = cfg.modem_dev;
    mc.baud = cfg.modem_baud;
    mc.type = cfg.modem_type;   // Auto → detected at start()
    return std::make_unique<AtModem>(std::move(mc));
}

} // namespace zerotouch
