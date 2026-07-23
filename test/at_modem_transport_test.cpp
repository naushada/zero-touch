#include "zerotouch/at_modem_transport.hpp"

#include <gtest/gtest.h>

#include "modem_mock.hpp"

using namespace zerotouch;

TEST(AtModemTransport, StartStartsModem) {
    MockModem m;
    AtModemTransport tx(m);
    tx.start();
    EXPECT_TRUE(m.started);
}

TEST(AtModemTransport, SendGoesToModem) {
    MockModem m;
    AtModemTransport tx(m);
    EXPECT_TRUE(tx.send("+15551230000", "OK GNMI GET /a=1"));
    ASSERT_EQ(m.sent_sms.size(), 1u);
    EXPECT_EQ(m.sent_sms[0].to, "+15551230000");
    EXPECT_EQ(m.sent_sms[0].text, "OK GNMI GET /a=1");
}

TEST(AtModemTransport, InboundIsDeliveredToCallback) {
    MockModem m;
    AtModemTransport tx(m);

    InboundSms got;
    int count = 0;
    tx.on_message([&](const InboundSms& in) { got = in; ++count; });
    tx.start();

    m.inject({"+919096383701", "IOT GNMI GET /system/config/hostname", "t0"});
    ASSERT_EQ(count, 1);
    EXPECT_EQ(got.sender, "+919096383701");
    EXPECT_EQ(got.text, "IOT GNMI GET /system/config/hostname");
}
