#ifndef __zerotouch_sim_gnmi_tree_hpp__
#define __zerotouch_sim_gnmi_tree_hpp__

#include <cstddef>
#include <map>
#include <string>
#include <vector>

/**
 * @file gnmi_tree.hpp
 * @brief The config-data tree `zt-gnmi-simd` serves: a flat xpath → value map
 *        with gNMI-ish subtree semantics.
 *
 * Header-only and dependency-free (no protobuf, no libevent, no Lua) on purpose:
 * the daemon that wraps it needs the whole gRPC stack, but the *lookup rules*
 * are where the demo's behaviour actually lives, so they stay host-testable and
 * are covered by the default suite (test/gnmi_tree_test.cpp).
 *
 * Seeding is deliberately NOT here: the seed is a Lua file, read through
 * `lua_file` in zt_gnmi_simd.cpp (project rules 2 and 3 — configuration is Lua,
 * and Lua I/O goes through lua_engine). This class only ever receives values
 * through set().
 */

namespace zerotouch {
namespace sim {

/// One leaf of the tree.
struct TreeEntry {
    std::string xpath;
    std::string value;
};

class GnmiTree {
public:
    void set(const std::string& xpath, const std::string& value) {
        m_kv[xpath] = value;
    }

    /// gNMI-ish read: an exact leaf if one exists, else every leaf under
    /// `xpath` as a subtree ("/" returns the whole tree). Empty when neither
    /// matches — the caller maps that to NOT_FOUND.
    std::vector<TreeEntry> lookup(const std::string& xpath) const {
        std::vector<TreeEntry> out;
        if (const auto it = m_kv.find(xpath); it != m_kv.end()) {
            out.push_back({it->first, it->second});
            return out;
        }
        // Subtree: "/" matches everything, otherwise "<xpath>/" is the prefix.
        // Matching on the trailing slash stops "/interfaces/interface[name=eth0]"
        // from also selecting "/interfaces/interface[name=eth01]/...".
        const std::string prefix = (xpath == "/") ? std::string("/") : xpath + "/";
        for (const auto& [k, v] : m_kv)
            if (k.rfind(prefix, 0) == 0) out.push_back({k, v});
        return out;
    }

    /// Delete an exact leaf or a whole subtree. Returns how many leaves went.
    std::size_t erase(const std::string& xpath) {
        if (const auto it = m_kv.find(xpath); it != m_kv.end()) {
            m_kv.erase(it);
            return 1;
        }
        const std::string prefix = (xpath == "/") ? std::string("/") : xpath + "/";
        std::size_t n = 0;
        for (auto it = m_kv.begin(); it != m_kv.end();) {
            if (it->first.rfind(prefix, 0) == 0) { it = m_kv.erase(it); ++n; }
            else                                 { ++it; }
        }
        return n;
    }

    const std::map<std::string, std::string>& all() const { return m_kv; }
    std::size_t size() const { return m_kv.size(); }

private:
    std::map<std::string, std::string> m_kv;
};

} // namespace sim
} // namespace zerotouch

#endif /* __zerotouch_sim_gnmi_tree_hpp__ */
