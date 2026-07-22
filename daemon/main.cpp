/// zero-touchd — authenticated device control + gNMI provisioning over SMS.
///
/// Reactor-driven (ACE). Consumes the MT-SMS envelope cellular-client publishes
/// (sms.version + sms.last.*), authenticates the sender against the device's own
/// users (auth.users.*), and either runs a gNMI Get/Set against the device-local
/// gNMI server (`IOT GNMI …`) or a classic smsctl command, then answers with one
/// MO SMS. It never talks to the modem and holds no extra privilege. Ships
/// disabled (zerotouch.enabled=false). See DESIGN.md.

#include <iostream>
#include <string>

#include "zerotouchd.hpp"

namespace {

void usage() {
    std::cout <<
        "zero-touchd — device control + gNMI provisioning over SMS\n"
        "\n"
        "Usage: zero-touchd [--ds-sock=PATH] [--help]\n"
        "\n"
        "  --ds-sock=PATH    ds-server unix socket (default: ds built-in).\n"
        "  --help            show this and exit.\n"
        "\n"
        "Behaviour is driven by the zerotouch.* ds keys (enabled, gnmi.port,\n"
        "allowed.numbers, session.ttl.sec, lockout.*), which hot-apply on change.\n"
        "The daemon is inert until zerotouch.enabled=true.\n";
}

} // namespace

int main(int argc, char** argv) {
    zerotouch::ZeroTouchClient::Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a.rfind("--ds-sock=", 0) == 0) cfg.ds_sock = a.substr(10);
        else if (a == "--help" || a == "-h")    { usage(); return 0; }
        else {
            std::cerr << "zero-touchd: unknown argument '" << a << "'\n";
            usage();
            return 2;
        }
    }

    zerotouch::ZeroTouchClient client(std::move(cfg));
    return client.run();
}
