#include "zerotouch/modem.hpp"

#include <gtest/gtest.h>

#include "modem_mock.hpp"

using namespace zerotouch;

TEST(Modem, LineWithFindsPrefix) {
    AtResult r;
    r.ok = true;
    r.lines = {"+CSQ: 17,99", "OK-ish"};
    EXPECT_EQ(r.line_with("+CSQ:"), "+CSQ: 17,99");
    EXPECT_EQ(r.line_with("+CREG:"), "");
}

TEST(Modem, MockRecordsAndReplies) {
    MockModem m;
    m.reply_ok("AT+CSQ", {"+CSQ: 20,99"});
    const auto r = m.at("AT+CSQ");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.line_with("+CSQ:"), "+CSQ: 20,99");
    ASSERT_EQ(m.sent_at.size(), 1u);
    EXPECT_EQ(m.sent_at[0], "AT+CSQ");

    // Unknown command → default bare OK.
    EXPECT_TRUE(m.at("AT+CFUN=1").ok);
}
