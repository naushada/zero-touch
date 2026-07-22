#ifndef __zerotouch_test_mocks_hpp__
#define __zerotouch_test_mocks_hpp__

#include <string>
#include <utility>
#include <vector>

#include "zerotouch/gnmi_sink.hpp"
#include "zerotouch/sms_transport.hpp"

/**
 * @file mocks.hpp
 * @brief In-memory ISmsTransport / GnmiSink doubles for the host test suite.
 *        No modem, no gRPC, no event loop.
 */

namespace zerotouch {

/// Records outbound replies and lets a test inject inbound messages.
class MockTransport : public ISmsTransport {
public:
    struct Sent { std::string to, text; };

    void on_message(MessageFn cb) override { m_cb = std::move(cb); }
    bool send(const std::string& to, const std::string& text) override {
        sent.push_back({to, text});
        return true;
    }
    void start() override { started = true; }

    /// Drive one inbound SMS through the registered callback.
    void inject(const InboundSms& in) { if (m_cb) m_cb(in); }

    std::vector<Sent> sent;
    bool              started = false;

private:
    MessageFn m_cb;
};

/// Canned GnmiSink: returns `next` for the following call and records requests.
class MockGnmiSink : public GnmiSink {
public:
    GnmiResult get(const std::vector<std::string>& xpaths) override {
        last_get = xpaths;
        return next;
    }
    GnmiResult set(
        const std::vector<std::pair<std::string, std::string>>& updates) override {
        last_set = updates;
        return next;
    }

    GnmiResult next;   ///< result the next call returns

    std::vector<std::string>                         last_get;
    std::vector<std::pair<std::string, std::string>> last_set;
};

} // namespace zerotouch

#endif /* __zerotouch_test_mocks_hpp__ */
