#include "zerotouch/users.hpp"

#include <gtest/gtest.h>

using namespace zerotouch;

TEST(Users, ParsesRecords) {
    const auto s = UserStore::parse(
        "# id:sha256:access\n"
        "admin:5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8:Admin\n"
        "alice:2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97bf527a25b:Viewer\n"
        "\n");
    EXPECT_EQ(s.size(), 2u);

    User u;
    ASSERT_TRUE(s.lookup("admin", u));
    EXPECT_EQ(u.access, "Admin");
    EXPECT_EQ(u.hash.size(), 64u);
    ASSERT_TRUE(s.lookup("alice", u));
    EXPECT_EQ(u.access, "Viewer");
    EXPECT_FALSE(s.lookup("bob", u));
}

TEST(Users, UnknownAccessDefaultsToViewer) {
    const auto s = UserStore::parse("carol:abc123:superuser\n");
    User u;
    ASSERT_TRUE(s.lookup("carol", u));
    EXPECT_EQ(u.access, "Viewer");
}

TEST(Users, MalformedLinesSkipped) {
    const auto s = UserStore::parse(
        "no-colons-here\n"
        "onlyone:field\n"
        ":emptyid:Admin\n"
        "noHash::Admin\n"
        "good:hash:Admin\n");
    EXPECT_EQ(s.size(), 1u);
    User u;
    EXPECT_TRUE(s.lookup("good", u));
}
