#include "zerotouch/users.hpp"

#include <sstream>

/**
 * @file users.cpp
 * @brief `id:hash:access` user-file parser. Pure; see users.hpp.
 */

namespace zerotouch {

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

UserStore UserStore::parse(const std::string& text) {
    UserStore store;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (const auto h = line.find('#'); h != std::string::npos) line.erase(h);
        line = trim(line);
        if (line.empty()) continue;

        // id:hash:access
        const auto c1 = line.find(':');
        if (c1 == std::string::npos) continue;
        const auto c2 = line.find(':', c1 + 1);
        if (c2 == std::string::npos) continue;

        User u;
        u.id   = trim(line.substr(0, c1));
        u.hash = trim(line.substr(c1 + 1, c2 - c1 - 1));
        u.access = trim(line.substr(c2 + 1));
        if (u.id.empty() || u.hash.empty()) continue;
        if (u.access != "Admin") u.access = "Viewer";

        store.m_users[u.id] = std::move(u);
    }
    return store;
}

bool UserStore::lookup(const std::string& id, User& out) const {
    auto it = m_users.find(id);
    if (it == m_users.end()) return false;
    out = it->second;
    return true;
}

} // namespace zerotouch
