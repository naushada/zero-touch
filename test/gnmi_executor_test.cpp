#include "zerotouch/gnmi_executor.hpp"

#include <gtest/gtest.h>

#include "mocks.hpp"

using namespace zerotouch;

namespace {
GnmiCommand get(std::initializer_list<const char*> xs) {
    GnmiCommand c;
    c.kind = GnmiKind::Get;
    c.xpaths = {xs.begin(), xs.end()};
    return c;
}
GnmiCommand set1(const char* x, const char* v) {
    GnmiCommand c;
    c.kind = GnmiKind::Set;
    c.updates = {{x, v}};
    return c;
}
GnmiResult okGet() {
    GnmiResult r;
    r.ok = true;
    r.paths = {{"/a", "1", ""}};
    return r;
}
} // namespace

TEST(GnmiExecutor, GetRequiresSession) {
    MockGnmiSink sink;
    GnmiExecutor ex(sink, [](const std::string&) { return Access::None; });
    EXPECT_EQ(ex.handle(get({"/a"}), "+123"), "ERR GNMI GET login required");
    EXPECT_TRUE(sink.last_get.empty());   // never reached the sink
}

TEST(GnmiExecutor, GetViewerOk) {
    MockGnmiSink sink;
    sink.next = okGet();
    GnmiExecutor ex(sink, [](const std::string&) { return Access::Viewer; });
    EXPECT_EQ(ex.handle(get({"/a"}), "+123"), "OK GNMI GET /a=1");
    ASSERT_EQ(sink.last_get.size(), 1u);
    EXPECT_EQ(sink.last_get[0], "/a");
}

TEST(GnmiExecutor, SetRejectsViewer) {
    MockGnmiSink sink;
    GnmiExecutor ex(sink, [](const std::string&) { return Access::Viewer; });
    EXPECT_EQ(ex.handle(set1("/a", "1"), "+123"), "ERR GNMI SET admin login required");
    EXPECT_TRUE(sink.last_set.empty());
}

TEST(GnmiExecutor, SetAdminOk) {
    MockGnmiSink sink;
    sink.next.ok = true;
    GnmiExecutor ex(sink, [](const std::string&) { return Access::Admin; });
    EXPECT_EQ(ex.handle(set1("/a", "1"), "+123"), "OK GNMI SET 1 path(s) updated");
    ASSERT_EQ(sink.last_set.size(), 1u);
    EXPECT_EQ(sink.last_set[0].first, "/a");
}

TEST(GnmiExecutor, UnknownReportsError) {
    MockGnmiSink sink;
    GnmiCommand c;
    c.kind = GnmiKind::Unknown;
    c.error = "gnmi set: xpath/value count mismatch";
    GnmiExecutor ex(sink, [](const std::string&) { return Access::Admin; });
    EXPECT_EQ(ex.handle(c, "+123"), "ERR gnmi set: xpath/value count mismatch");
}
