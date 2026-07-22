#include "zerotouch/direct_action_sink.hpp"

#include <gtest/gtest.h>

#include "modem_mock.hpp"

#include "smsctl/parser.hpp"
#include "smsctl/session.hpp"

using namespace zerotouch;

namespace {
bool has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& e : v) if (e == s) return true;
    return false;
}
} // namespace

// ── DirectActionSink as a unit (against the mock modem) ─────────────────────

TEST(DirectActionSink, ApnIssuesCgdcont) {
    MockModem m;
    DirectActionSink sink(m);
    EXPECT_TRUE(sink.set("cell.apn", "internet"));
    EXPECT_TRUE(has(m.sent_at, "AT+CGDCONT=1,\"IP\",\"internet\""));
}

TEST(DirectActionSink, ResetCyclesCfun) {
    MockModem m;
    DirectActionSink sink(m);
    EXPECT_TRUE(sink.set("cell.reset.request", "12345"));
    EXPECT_TRUE(has(m.sent_at, "AT+CFUN=0"));
    EXPECT_TRUE(has(m.sent_at, "AT+CFUN=1"));
}

TEST(DirectActionSink, StatusReadsMapToAt) {
    MockModem m;
    m.reply_ok("AT+CREG?", {"+CREG: 0,1"});
    m.reply_ok("AT+CSQ",   {"+CSQ: 17,99"});
    m.reply_ok("AT+CGPADDR=1", {"+CGPADDR: 1,\"10.1.2.3\""});
    DirectActionSink sink(m);

    EXPECT_EQ(sink.get("cell.reg").value_or("-"), "home");
    EXPECT_EQ(sink.get("cell.signal.dbm").value_or("-"), "-79");  // -113 + 2*17
    EXPECT_EQ(sink.get("cell.ip").value_or("-"), "10.1.2.3");
    EXPECT_FALSE(sink.get("vpn.state").has_value());              // → "-"
    EXPECT_FALSE(sink.get("wifi.assoc.ssid").has_value());
}

TEST(DirectActionSink, UnknownSignalIsAbsent) {
    MockModem m;
    m.reply_ok("AT+CSQ", {"+CSQ: 99,99"});   // 99 = not known/detectable
    DirectActionSink sink(m);
    EXPECT_FALSE(sink.get("cell.signal.dbm").has_value());
}

TEST(DirectActionSink, RebootTriggerCallsInjectedFn) {
    MockModem m;
    bool rebooted = false;
    DirectActionSink sink(m, [&] { rebooted = true; return true; });
    EXPECT_TRUE(sink.arm_trigger(smsctl::kRebootTrigger, "reboot\n"));
    EXPECT_TRUE(rebooted);
    // no reboot fn → cannot arm
    DirectActionSink bare(m);
    EXPECT_FALSE(bare.arm_trigger(smsctl::kRebootTrigger, "reboot\n"));
}

// ── end-to-end through the reused smsctl::Executor ──────────────────────────

TEST(DirectActionSink, ApnCommandThroughExecutorHitsModem) {
    MockModem m;
    DirectActionSink sink(m);
    smsctl::SessionStore sessions;

    std::map<std::string, smsctl::Account> users = {
        {"admin", {"admin", smsctl::sha256_hex("admin"), "Admin"}},
    };
    auto lookup = [&](const std::string& id, smsctl::Account& out) {
        auto it = users.find(id);
        if (it == users.end()) return false;
        out = it->second;
        return true;
    };
    smsctl::Executor ex(sink, sessions, lookup);
    const std::string sender = "+15551230000";

    ex.handle(smsctl::parse("IOT LOGIN admin admin"), sender, 1000, 1);
    const auto reply = ex.handle(smsctl::parse("IOT APN internet"), sender, 1000, 2);

    EXPECT_EQ(reply.compare(0, 2, "OK"), 0);
    EXPECT_TRUE(has(m.sent_at, "AT+CGDCONT=1,\"IP\",\"internet\""));
    EXPECT_TRUE(has(m.sent_at, "AT+CFUN=1"));   // radio cycle applied it
}
