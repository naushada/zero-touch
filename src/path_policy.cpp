#include "zerotouch/path_policy.hpp"

#include <algorithm>
#include <cctype>

/**
 * @file path_policy.cpp
 * @brief Sensitive-path denylist. Pure; see path_policy.hpp.
 */

namespace zerotouch {

const std::vector<std::string>& default_deny_tokens() {
    // Substrings that commonly appear in YANG leaves carrying a secret. Kept
    // lowercase; matching lowercases the xpath first.
    static const std::vector<std::string> kTokens = {
        "password", "passwd", "psk", "secret", "private-key", "privatekey",
        "credential", "pre-shared", "apikey", "api-key", "auth-key",
    };
    return kTokens;
}

bool is_sensitive_path(const std::string& xpath,
                       const std::vector<std::string>& tokens) {
    std::string low = xpath;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (const auto& t : tokens)
        if (!t.empty() && low.find(t) != std::string::npos) return true;
    return false;
}

bool is_sensitive_path(const std::string& xpath) {
    return is_sensitive_path(xpath, default_deny_tokens());
}

} // namespace zerotouch
