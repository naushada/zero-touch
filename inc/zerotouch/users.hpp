#ifndef __zerotouch_users_hpp__
#define __zerotouch_users_hpp__

#include <cstddef>
#include <map>
#include <string>

/**
 * @file users.hpp
 * @brief Local user store for the standalone appliance — replaces the ds
 *        `auth.users.*` keys. Stores pre-hashed credentials, so it needs no
 *        crypto itself; the SessionStore hashes the incoming password and
 *        compares. Pure and host-testable.
 *
 * File format: one `id:sha256hex:access` record per line; `#` starts a comment;
 * blank/malformed lines are skipped. `access` is "Admin" or "Viewer" (anything
 * else is treated as "Viewer").
 */

namespace zerotouch {

struct User {
    std::string id;
    std::string hash;    ///< lowercase sha256 hex of the password
    std::string access;  ///< "Admin" | "Viewer"
};

class UserStore {
public:
    /// Parse the file *contents* (not a path). Later duplicate ids win.
    static UserStore parse(const std::string& text);

    /// Resolve id → record. Returns false when unknown.
    bool lookup(const std::string& id, User& out) const;

    std::size_t size() const { return m_users.size(); }

private:
    std::map<std::string, User> m_users;
};

} // namespace zerotouch

#endif /* __zerotouch_users_hpp__ */
