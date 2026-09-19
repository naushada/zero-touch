// echo_client.cc — a one-shot application client, for driving the demo by hand.
//
// Runs inside the SERVER container and dials the forwarder, so every RPC it
// makes travels the full path: forwarder -> tunnel -> client container ->
// Echo service, and back. `./run.sh rpc "hello"` is this binary.
#include <cstdlib>
#include <cstring>
#include <string>

#include "echo_call.h"
#include "log.h"

namespace {

bool HasFlag(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i < argc; ++i)
        if (argv[i] == flag) return true;
    return false;
}

const char* Arg(int argc, char** argv, const std::string& flag,
                const char* fallback) {
    const std::string prefix = flag + "=";
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == flag && i + 1 < argc) return argv[i + 1];
        if (strncmp(argv[i], prefix.c_str(), prefix.size()) == 0) {
            return argv[i] + prefix.size();
        }
    }
    return fallback;
}

}  // namespace

int main(int argc, char** argv) {
    gt::LogTag() = "rpc";

    const std::string addr    = Arg(argc, argv, "--addr", "127.0.0.1:50052");
    const std::string message = Arg(argc, argv, "--message", "hello");
    const int count           = std::atoi(Arg(argc, argv, "--count", "3"));
    const bool do_stream      = HasFlag(argc, argv, "--stream");

    gt::EchoCaller caller(addr);
    LOG("dialling %s (the tunnel forwarder)", addr.c_str());

    std::string reply, error;
    if (!caller.Say(message, &reply, &error)) {
        LOG("Say FAILED: %s", error.c_str());
        return 1;
    }
    LOG("Say -> %s", reply.c_str());

    if (do_stream) {
        LOG("SayStream x%d…", count);
        if (!caller.SayStream(message, static_cast<uint32_t>(count), &error)) {
            LOG("SayStream FAILED: %s", error.c_str());
            return 1;
        }
    }
    return 0;
}
