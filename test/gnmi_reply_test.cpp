#include "zerotouch/gnmi_reply.hpp"

#include <gtest/gtest.h>

using namespace zerotouch;

TEST(GnmiReply, GetOkFormatsPairs) {
    GnmiResult r;
    r.ok = true;
    r.grpc_status = 0;
    r.paths = {{"/a", "1", ""}, {"/b", "hi", ""}};
    EXPECT_EQ(format_get(r), "OK GNMI GET /a=1; /b=hi");
}

TEST(GnmiReply, GetPerPathErrorHidesValue) {
    GnmiResult r;
    r.ok = true;
    r.paths = {{"/secret", "hunter2", "denied"}};
    const auto out = format_get(r);
    EXPECT_EQ(out, "OK GNMI GET /secret=<denied>");
    EXPECT_EQ(out.find("hunter2"), std::string::npos);
}

TEST(GnmiReply, GetMixedGoodAndDeniedInOneReply) {
    // RPC ok=true; one leaf returned, one path denied. Both appear; the denied
    // one is masked. (Regression: ok must stay true so the OK branch renders.)
    GnmiResult r;
    r.ok = true;
    r.paths = {{"/system/config/hostname", "router-7", ""},
               {"/aaa/user[name=admin]/password", "s3cr3t", "sensitive path denied"}};
    const auto out = format_get(r);
    EXPECT_EQ(out,
              "OK GNMI GET /system/config/hostname=router-7; "
              "/aaa/user[name=admin]/password=<sensitive path denied>");
    EXPECT_EQ(out.find("s3cr3t"), std::string::npos);
}

TEST(GnmiReply, GetTransportError) {
    GnmiResult r;
    r.ok = false;
    r.grpc_message = "unavailable";
    EXPECT_EQ(format_get(r), "ERR GNMI GET unavailable");
}

TEST(GnmiReply, SetOkCountsPaths) {
    GnmiResult r;
    r.ok = true;
    EXPECT_EQ(format_set(r, 2), "OK GNMI SET 2 path(s) updated");
}

TEST(GnmiReply, SetError) {
    GnmiResult r;
    r.ok = false;
    r.grpc_message = "invalid-argument";
    EXPECT_EQ(format_set(r, 1), "ERR GNMI SET invalid-argument");
}

TEST(GnmiReply, ClampAddsEllipsis) {
    const std::string big(300, 'x');
    const auto out = clamp_sms(big);
    EXPECT_EQ(out.size(), kMaxReply);
    EXPECT_EQ(out.substr(out.size() - 3), "...");
}
