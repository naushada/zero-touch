#include "zerotouch/bridge.hpp"

#include <gtest/gtest.h>

#include <sstream>

#include "mocks.hpp"

using namespace zerotouch;

namespace {
/// Minimal whitespace tokenizer standing in for smsctl::tokenize in tests.
std::vector<std::string> ws_tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string t;
    while (ss >> t) out.push_back(t);
    return out;
}
} // namespace

TEST(Bridge, GnmiGetIsExecutedAndReplied) {
    MockTransport tx;
    MockGnmiSink sink;
    sink.next.ok = true;
    sink.next.paths = {{"/a", "1", ""}};
    GnmiExecutor gex(sink, [](const std::string&) { return Access::Viewer; });

    Bridge b(tx, gex, ws_tokenize);
    b.start();
    EXPECT_TRUE(tx.started);

    tx.inject({"+123", "IOT GNMI GET /a", "t0"});
    ASSERT_EQ(tx.sent.size(), 1u);
    EXPECT_EQ(tx.sent[0].to, "+123");
    EXPECT_EQ(tx.sent[0].text, "OK GNMI GET /a=1");
}

TEST(Bridge, NonGnmiFallsBackToClassicEngine) {
    MockTransport tx;
    MockGnmiSink sink;
    GnmiExecutor gex(sink, [](const std::string&) { return Access::Admin; });

    bool called = false;
    Bridge b(tx, gex, ws_tokenize,
             [&](const std::string&, const std::string& text) {
                 called = true;
                 return std::string("OK ") + text;
             });
    b.on_sms({"+123", "IOT STATUS", "t0"});

    EXPECT_TRUE(called);
    ASSERT_EQ(tx.sent.size(), 1u);
    EXPECT_EQ(tx.sent[0].text, "OK IOT STATUS");
    EXPECT_TRUE(sink.last_get.empty());   // never touched the gnmi sink
}

TEST(Bridge, NonGnmiWithoutFallbackIsDroppedSilently) {
    MockTransport tx;
    MockGnmiSink sink;
    GnmiExecutor gex(sink, [](const std::string&) { return Access::None; });

    Bridge b(tx, gex, ws_tokenize);   // no fallback
    b.on_sms({"+123", "just some text", "t0"});
    EXPECT_TRUE(tx.sent.empty());
}

TEST(Bridge, EmptyReplyIsNotSent) {
    MockTransport tx;
    MockGnmiSink sink;
    GnmiExecutor gex(sink, [](const std::string&) { return Access::Admin; });

    // Fallback returns "" → drop, no SMS.
    Bridge b(tx, gex, ws_tokenize,
             [](const std::string&, const std::string&) { return std::string(); });
    b.on_sms({"+123", "IOT LOGIN alice secret", "t0"});
    EXPECT_TRUE(tx.sent.empty());
}
