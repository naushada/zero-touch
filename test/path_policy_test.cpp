#include "zerotouch/path_policy.hpp"

#include <gtest/gtest.h>

using namespace zerotouch;

TEST(PathPolicy, DefaultDenylistCatchesSecrets) {
    EXPECT_TRUE(is_sensitive_path("/system/aaa/authentication/users/user[name=admin]/config/password"));
    EXPECT_TRUE(is_sensitive_path("/wifi/networks/network[ssid=home]/config/psk"));
    EXPECT_TRUE(is_sensitive_path("/ipsec/config/pre-shared-key"));
    EXPECT_TRUE(is_sensitive_path("/system/config/api-key"));
}

TEST(PathPolicy, CaseInsensitive) {
    EXPECT_TRUE(is_sensitive_path("/System/Config/PASSWORD"));
    EXPECT_TRUE(is_sensitive_path("/a/Secret/b"));
}

TEST(PathPolicy, OrdinaryPathsAllowed) {
    EXPECT_FALSE(is_sensitive_path("/interfaces/interface[name=eth0]/state/oper-status"));
    EXPECT_FALSE(is_sensitive_path("/system/config/hostname"));
    EXPECT_FALSE(is_sensitive_path("/"));
}

TEST(PathPolicy, ContainerPathDoesNotInheritItsChildrensSensitivity) {
    // The asymmetry that makes response-side filtering mandatory in
    // LocalGnmiSink::get: these container paths are NOT sensitive, so they sail
    // through the request-side check — but a gNMI Get on them returns the
    // subtree, which does carry the secret leaves below. Screening only what was
    // asked for would therefore leak. See local_gnmi_sink.hpp.
    EXPECT_FALSE(is_sensitive_path("/system/aaa"));
    EXPECT_FALSE(is_sensitive_path("/system"));
    EXPECT_FALSE(is_sensitive_path("/"));
    // …while the leaves the server returns underneath them are caught.
    EXPECT_TRUE(is_sensitive_path("/system/aaa/user[name=admin]/config/password"));
}

TEST(PathPolicy, CustomTokens) {
    const std::vector<std::string> tokens = {"mrn", "iccid"};
    EXPECT_TRUE(is_sensitive_path("/sim/config/iccid", tokens));
    EXPECT_FALSE(is_sensitive_path("/system/config/password", tokens)); // not in custom set
}

TEST(PathPolicy, EmptyTokenIsIgnored) {
    const std::vector<std::string> tokens = {""};   // must not match everything
    EXPECT_FALSE(is_sensitive_path("/anything", tokens));
}
