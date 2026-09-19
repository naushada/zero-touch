# grpc-tunnel — gRPC-over-gRPC reverse tunnel, in C++.
#
# Three binaries out of one tree:
#   tunnel-server   tunnel gRPC server + TCP forwarder (+ the app's gRPC client)
#   tunnel-client   tunnel gRPC client + the app's gRPC server
#   echo-rpc        one-shot app client, for driving the demo by hand
#
# gRPC is found through pkg-config rather than find_package(gRPC): Debian's
# libgrpc++-dev ships .pc files but no gRPCConfig.cmake, and the container
# build is Debian.
cmake_minimum_required(VERSION 3.16)
project(grpc_tunnel CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
find_package(Threads REQUIRED)

find_program(PROTOC protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# Both the message and the service stubs, per proto.
set(PROTO_SRCS "")
foreach(name tunnel echo)
    set(proto "${CMAKE_CURRENT_SOURCE_DIR}/proto/${name}.proto")
    add_custom_command(
        OUTPUT "${GENERATED_DIR}/${name}.pb.cc" "${GENERATED_DIR}/${name}.pb.h"
               "${GENERATED_DIR}/${name}.grpc.pb.cc" "${GENERATED_DIR}/${name}.grpc.pb.h"
        COMMAND ${PROTOC}
                --proto_path "${CMAKE_CURRENT_SOURCE_DIR}/proto"
                --cpp_out "${GENERATED_DIR}"
                --grpc_out "${GENERATED_DIR}"
                --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                "${proto}"
        DEPENDS "${proto}"
        COMMENT "protoc ${name}.proto"
        VERBATIM)
    list(APPEND PROTO_SRCS
         "${GENERATED_DIR}/${name}.pb.cc" "${GENERATED_DIR}/${name}.grpc.pb.cc")
endforeach()

# Everything both binaries share: the generated stubs, sockets, and the
# session multiplexer that turns TCP connections into tunnel streams.
add_library(tunnel_core STATIC
    ${PROTO_SRCS}
    src/net.cc
    src/session.cc)
target_include_directories(tunnel_core PUBLIC "${GENERATED_DIR}" src)
target_link_libraries(tunnel_core PUBLIC
    PkgConfig::GRPCPP PkgConfig::PROTOBUF Threads::Threads)

add_library(echo_app STATIC src/echo_call.cc src/echo_service.cc)
target_link_libraries(echo_app PUBLIC tunnel_core)

add_executable(tunnel-server src/tunnel_server.cc)
target_link_libraries(tunnel-server PRIVATE echo_app)

add_executable(tunnel-client src/tunnel_client.cc)
target_link_libraries(tunnel-client PRIVATE echo_app)

add_executable(echo-rpc src/echo_client.cc)
target_link_libraries(echo-rpc PRIVATE echo_app)

install(TARGETS tunnel-server tunnel-client echo-rpc RUNTIME DESTINATION bin)
