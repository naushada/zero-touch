#ifndef __zerotouch_test_modem_mock_hpp__
#define __zerotouch_test_modem_mock_hpp__

#include <map>
#include <string>
#include <vector>

#include "zerotouch/modem.hpp"

/**
 * @file modem_mock.hpp
 * @brief Scriptable IModem double: canned AT responses + recorded traffic.
 *        Pure (only needs modem.hpp), so both test suites can use it.
 */

namespace zerotouch {

class MockModem : public IModem {
public:
    // Script a response for an exact AT command string.
    void reply(const std::string& cmd, AtResult r) { m_replies[cmd] = std::move(r); }
    void reply_ok(const std::string& cmd, std::vector<std::string> lines = {}) {
        m_replies[cmd] = AtResult{true, std::move(lines)};
    }

    AtResult at(const std::string& cmd) override {
        sent_at.push_back(cmd);
        auto it = m_replies.find(cmd);
        if (it != m_replies.end()) return it->second;
        return AtResult{true, {}};   // default: bare OK
    }
    bool send_sms(const std::string& to, const std::string& text) override {
        sent_sms.push_back({to, text});
        return true;
    }
    void on_sms(SmsFn cb) override { m_cb = std::move(cb); }
    void start() override { started = true; }

    /// Drive an inbound SMS through the registered callback (Phase 2 tests).
    void inject(const InboundSms& in) { if (m_cb) m_cb(in); }

    struct Sms { std::string to, text; };
    std::vector<std::string> sent_at;
    std::vector<Sms>         sent_sms;
    bool                     started = false;

private:
    std::map<std::string, AtResult> m_replies;
    SmsFn                           m_cb;
};

} // namespace zerotouch

#endif /* __zerotouch_test_modem_mock_hpp__ */
