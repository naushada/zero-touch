// echo_service.h — the application-side gRPC SERVER.
//
// The mirror of echo_call.h, and just as unaware: it binds a normal port and
// answers normal RPCs. That the calls arrive out of a tunnel rather than off
// the network is invisible from in here.
#pragma once

#include "echo.grpc.pb.h"

namespace gt {

class EchoServiceImpl final : public echo::Echo::Service {
public:
    grpc::Status Say(grpc::ServerContext* ctx, const echo::SayRequest* req,
                     echo::SayReply* res) override;

    grpc::Status SayStream(grpc::ServerContext* ctx,
                           const echo::SayRequest* req,
                           grpc::ServerWriter<echo::SayReply>* writer) override;
};

}  // namespace gt
