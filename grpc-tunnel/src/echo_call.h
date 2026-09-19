// echo_call.h — the application-side gRPC CLIENT.
//
// Deliberately boring: an ordinary channel to an ordinary host:port. It is
// pointed at the tunnel server's local forwarder instead of at the real
// service, and that substitution is the only thing tunnelling costs the
// application. No tunnel header is included here on purpose.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "echo.grpc.pb.h"

namespace gt {

class EchoCaller {
public:
    explicit EchoCaller(const std::string& addr);

    // Unary. `reply` gets the peer's answer; `error` the failure text.
    bool Say(const std::string& message, std::string* reply, std::string* error);

    // Server-streaming, to prove the tunnel carries more than request/response.
    bool SayStream(const std::string& message, uint32_t count,
                   std::string* error);

private:
    std::string addr_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<echo::Echo::Stub> stub_;
};

}  // namespace gt
