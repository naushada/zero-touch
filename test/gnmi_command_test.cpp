#include "zerotouch/gnmi_command.hpp"

#include <gtest/gtest.h>

using namespace zerotouch;

namespace {
std::vector<std::string> toks(std::initializer_list<const char*> l) {
    return {l.begin(), l.end()};
}
} // namespace

TEST(GnmiCommand, NonGnmiIsPassedThrough) {
    EXPECT_EQ(parse_gnmi(toks({"IOT", "STATUS"})).kind, GnmiKind::NotGnmi);
    EXPECT_EQ(parse_gnmi(toks({"hello"})).kind, GnmiKind::NotGnmi);
    EXPECT_EQ(parse_gnmi({}).kind, GnmiKind::NotGnmi);
}

TEST(GnmiCommand, PrefixIsCaseInsensitive) {
    EXPECT_EQ(parse_gnmi(toks({"iot", "GnMi", "get", "/a"})).kind, GnmiKind::Get);
}

TEST(GnmiCommand, GetSingleAndCsv) {
    auto g = parse_gnmi(toks({"IOT", "GNMI", "GET", "/a,/b,/c"}));
    ASSERT_EQ(g.kind, GnmiKind::Get);
    ASSERT_EQ(g.xpaths.size(), 3u);
    EXPECT_EQ(g.xpaths[1], "/b");
}

TEST(GnmiCommand, GetMissingXpathIsUnknown) {
    EXPECT_EQ(parse_gnmi(toks({"IOT", "GNMI", "GET"})).kind, GnmiKind::Unknown);
}

TEST(GnmiCommand, SetPairsPositionally) {
    auto s = parse_gnmi(toks({"IOT", "GNMI", "SET", "/a,/b", "1,2"}));
    ASSERT_EQ(s.kind, GnmiKind::Set);
    ASSERT_EQ(s.updates.size(), 2u);
    EXPECT_EQ(s.updates[0].first, "/a");
    EXPECT_EQ(s.updates[0].second, "1");
    EXPECT_EQ(s.updates[1].first, "/b");
    EXPECT_EQ(s.updates[1].second, "2");
}

TEST(GnmiCommand, SetCountMismatchIsUnknown) {
    auto s = parse_gnmi(toks({"IOT", "GNMI", "SET", "/a,/b", "1"}));
    EXPECT_EQ(s.kind, GnmiKind::Unknown);
    EXPECT_FALSE(s.error.empty());
}

TEST(GnmiCommand, UnknownOp) {
    EXPECT_EQ(parse_gnmi(toks({"IOT", "GNMI", "frobnicate"})).kind, GnmiKind::Unknown);
}
