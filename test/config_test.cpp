#include "zerotouch/config.hpp"

#include <gtest/gtest.h>

using namespace zerotouch;

TEST(Config, DefaultsWhenEmpty) {
    const auto c = parse_config("");
    EXPECT_FALSE(c.enabled);
    EXPECT_EQ(c.gnmi_host, "127.0.0.1");
    EXPECT_EQ(c.gnmi_port, 50051);
    EXPECT_EQ(c.session_ttl_sec, 600u);
    EXPECT_TRUE(c.allowed_numbers.empty());
}

TEST(Config, ParsesKnownKeys) {
    const auto c = parse_config(
        "enabled = true\n"
        "gnmi.host = 127.0.0.1\n"
        "gnmi.port = 57400\n"
        "allowed.numbers = +919096383701, +4915112345678\n"
        "session.ttl.sec = 300\n"
        "modem.dev = /dev/ttyUSB3\n"
        "modem.baud = 921600\n");
    EXPECT_TRUE(c.enabled);
    EXPECT_EQ(c.gnmi_port, 57400);
    ASSERT_EQ(c.allowed_numbers.size(), 2u);
    EXPECT_EQ(c.allowed_numbers[0], "+919096383701");
    EXPECT_EQ(c.session_ttl_sec, 300u);
    EXPECT_EQ(c.modem_dev, "/dev/ttyUSB3");
    EXPECT_EQ(c.modem_baud, 921600u);
}

TEST(Config, CommentsAndBlankLinesIgnored) {
    const auto c = parse_config(
        "# a comment\n"
        "\n"
        "enabled = yes   # inline comment\n"
        "   \n"
        "bogus line with no equals\n"
        "unknown.key = whatever\n");
    EXPECT_TRUE(c.enabled);
    EXPECT_EQ(c.gnmi_port, 50051);   // untouched
}

TEST(Config, BoolForms) {
    EXPECT_TRUE(parse_config("enabled = 1").enabled);
    EXPECT_TRUE(parse_config("enabled = on").enabled);
    EXPECT_FALSE(parse_config("enabled = 0").enabled);
    EXPECT_FALSE(parse_config("enabled = nope").enabled);
}
