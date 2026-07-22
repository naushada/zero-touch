#include "zerotouch/sms_util.hpp"

/**
 * @file sms_util.cpp
 * @brief Pure SMS-transport helpers. See sms_util.hpp.
 */

namespace zerotouch {

bool is_reachable_sender(const std::string& sender) {
    std::size_t digits = 0;
    for (char c : sender) {
        if (c >= '0' && c <= '9') ++digits;
        else if (c != '+' && c != ' ' && c != '-') return false;
    }
    return digits >= 7;
}

} // namespace zerotouch
