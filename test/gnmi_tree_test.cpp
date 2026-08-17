#include <gtest/gtest.h>

#include "gnmi_tree.hpp"

/**
 * @file gnmi_tree_test.cpp
 * @brief The lookup rules zt-gnmi-simd serves. Pure — the daemon around this
 *        needs protobuf/libevent, these rules do not, so they stay covered by
 *        the default host suite.
 */

using zerotouch::sim::GnmiTree;

namespace {

GnmiTree seeded() {
    GnmiTree t;
    t.set("/system/config/hostname", "demo-router");
    t.set("/system/config/domain-name", "example.net");
    t.set("/system/state/boot-time", "1755400000");
    t.set("/interfaces/interface[name=eth0]/config/enabled", "true");
    t.set("/interfaces/interface[name=eth01]/config/enabled", "false");
    return t;
}

} // namespace

TEST(GnmiTree, ExactLeafWins) {
    const auto rows = seeded().lookup("/system/config/hostname");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].xpath, "/system/config/hostname");
    EXPECT_EQ(rows[0].value, "demo-router");
}

TEST(GnmiTree, NonLeafReturnsSubtree) {
    const auto rows = seeded().lookup("/system/config");
    ASSERT_EQ(rows.size(), 2u);   // hostname + domain-name, not boot-time
    for (const auto& r : rows)
        EXPECT_EQ(r.xpath.rfind("/system/config/", 0), 0u);
}

TEST(GnmiTree, RootReturnsEverything) {
    EXPECT_EQ(seeded().lookup("/").size(), seeded().size());
}

TEST(GnmiTree, SubtreePrefixDoesNotStrattleSiblingNames) {
    // "[name=eth0]" must not also select "[name=eth01]" — the trailing slash in
    // the prefix is what prevents it.
    const auto rows = seeded().lookup("/interfaces/interface[name=eth0]");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].xpath, "/interfaces/interface[name=eth0]/config/enabled");
    EXPECT_EQ(rows[0].value, "true");
}

TEST(GnmiTree, UnknownPathIsEmpty) {
    EXPECT_TRUE(seeded().lookup("/nope/not/here").empty());
}

TEST(GnmiTree, SetInsertsAndOverwrites) {
    GnmiTree t = seeded();
    t.set("/system/config/hostname", "router-7");
    EXPECT_EQ(t.lookup("/system/config/hostname")[0].value, "router-7");

    const std::size_t before = t.size();
    t.set("/system/config/new-leaf", "x");
    EXPECT_EQ(t.size(), before + 1);
}

TEST(GnmiTree, EraseLeafAndSubtree) {
    GnmiTree t = seeded();
    EXPECT_EQ(t.erase("/system/config/hostname"), 1u);
    EXPECT_TRUE(t.lookup("/system/config/hostname").empty());

    EXPECT_EQ(t.erase("/system"), 2u);   // domain-name + boot-time
    EXPECT_TRUE(t.lookup("/system").empty());

    EXPECT_EQ(t.erase("/gone"), 0u);
}
