#include "zerotouch/sms_util.hpp"

#include <gtest/gtest.h>

using namespace zerotouch;

TEST(SmsUtil, E164NumbersAreReachable) {
    EXPECT_TRUE(is_reachable_sender("+14155550123"));
    EXPECT_TRUE(is_reachable_sender("+91 98765 43210"));
    EXPECT_TRUE(is_reachable_sender("447700900123"));
    EXPECT_TRUE(is_reachable_sender("+1-415-555-0123"));
}

TEST(SmsUtil, AlphanumericSenderIdsAreNotReachable) {
    EXPECT_FALSE(is_reachable_sender("AZ-AIRTEL-S"));
    EXPECT_FALSE(is_reachable_sender("VODAFONE"));
    EXPECT_FALSE(is_reachable_sender("Amazon"));
}

TEST(SmsUtil, TooFewDigitsIsNotReachable) {
    EXPECT_FALSE(is_reachable_sender("12345"));    // only 5 digits
    EXPECT_FALSE(is_reachable_sender("+123456"));  // only 6 digits
    EXPECT_FALSE(is_reachable_sender(""));
}

TEST(SmsUtil, SevenDigitsIsTheBoundary) {
    EXPECT_TRUE(is_reachable_sender("1234567"));   // exactly 7
}
