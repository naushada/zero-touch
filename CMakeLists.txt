cmake_minimum_required(VERSION 3.16.3)
project(zero-touch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ZT_BUILD_TESTS  "Build the zero-touch gtest suite"        ON)
option(ZT_BUILD_GNMI   "Build LocalGnmiSink (needs protobuf/libevent/nghttp2)" OFF)
option(ZT_BUILD_DAEMON "Build the zero-touchd SMS→gNMI daemon"   OFF)

# The daemon needs the real gNMI backend.
if(ZT_BUILD_DAEMON)
    set(ZT_BUILD_GNMI ON)
endif()

# ── reused engine: smsctl::core from the iot submodule ──────────────────────
# Pulled in as a subproject; its own CMake exports the smsctl::core alias.
# iot source is NEVER modified — we only add_subdirectory the module path.
# EXCLUDE_FROM_ALL: only the daemon (Phase 6) depends on it, so the pure-core
# build does not require iot's nested json/ACE submodules.
set(IOT_SMSCTL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iot/modules/smsctl)
if(EXISTS ${IOT_SMSCTL_DIR}/CMakeLists.txt)
    add_subdirectory(${IOT_SMSCTL_DIR} ${CMAKE_BINARY_DIR}/smsctl EXCLUDE_FROM_ALL)
else()
    message(WARNING "iot submodule missing — run: git submodule update --init")
endif()

# ── zerotouch_core: pure command layer (gnmi parser/executor/reply/bridge) ──
# Talks only to ISmsTransport / GnmiSink and injected Auth/Tokenize/Fallback
# callbacks, so it carries NO compile-time smsctl dependency and is fully
# host-testable against mocks. The daemon links smsctl::core, not this lib.
add_library(zerotouch_core STATIC
    src/gnmi_command.cpp
    src/gnmi_executor.cpp
    src/gnmi_reply.cpp
    src/path_policy.cpp
    src/bridge.cpp)
target_include_directories(zerotouch_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_compile_options(zerotouch_core PRIVATE -Wall -Wextra)
add_library(zerotouch::core ALIAS zerotouch_core)

# ── zerotouch_gnmi: LocalGnmiSink over grace-server's gnmi_client ────────────
# Heavy deps (protobuf, libevent, nghttp2), so it is opt-in. Reuses the
# grace-server framework sources + gnmi/gnmi_ext protos verbatim.
if(ZT_BUILD_GNMI)
    find_package(Protobuf REQUIRED)
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBEVENT REQUIRED libevent)
    pkg_check_modules(LIBEVENT_OPENSSL REQUIRED libevent_openssl)
    pkg_check_modules(NGHTTP2 REQUIRED libnghttp2)

    set(GS_APP  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/grace-server/hackthon/app)
    set(GS_IDL  ${GS_APP}/idl)
    set(ZT_PROTO_GEN ${CMAKE_BINARY_DIR}/proto_gen)
    file(MAKE_DIRECTORY ${ZT_PROTO_GEN})

    # gnmi imports gnmi_ext — generate both (order matters).
    set(ZT_PROTO_SRCS
        ${ZT_PROTO_GEN}/gnmi_ext/gnmi_ext.pb.cc
        ${ZT_PROTO_GEN}/gnmi/gnmi.pb.cc)
    add_custom_command(
        OUTPUT  ${ZT_PROTO_SRCS}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --cpp_out=${ZT_PROTO_GEN}
                -I${GS_IDL} -I${Protobuf_INCLUDE_DIRS}
                ${GS_IDL}/gnmi_ext/gnmi_ext.proto ${GS_IDL}/gnmi/gnmi.proto
        DEPENDS ${GS_IDL}/gnmi/gnmi.proto ${GS_IDL}/gnmi_ext/gnmi_ext.proto
        COMMENT "Generating C++ from gNMI proto (gnmi, gnmi_ext)"
        VERBATIM)

    add_library(zt_gnmi_proto STATIC ${ZT_PROTO_SRCS})
    target_include_directories(zt_gnmi_proto PUBLIC
        ${ZT_PROTO_GEN} ${Protobuf_INCLUDE_DIRS})
    target_link_libraries(zt_gnmi_proto PUBLIC ${Protobuf_LIBRARIES} z)

    # Reused grace-server framework + gnmi_client (never modified).
    add_library(zerotouch_gnmi STATIC
        src/local_gnmi_sink.cpp
        ${GS_APP}/src/gnmi_client.cpp
        ${GS_APP}/src/grpc_session.cpp
        ${GS_APP}/src/framework.cpp
        ${GS_APP}/src/http2.cpp)
    target_include_directories(zerotouch_gnmi PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc)
    target_include_directories(zerotouch_gnmi PRIVATE
        ${GS_APP}/inc ${GS_APP}/openvpn/inc
        ${NGHTTP2_INCLUDE_DIRS} ${LIBEVENT_INCLUDE_DIRS})
    target_link_libraries(zerotouch_gnmi PUBLIC
        zerotouch_core zt_gnmi_proto
        ${LIBEVENT_OPENSSL_LIBRARIES} ${LIBEVENT_LIBRARIES}
        ${NGHTTP2_LIBRARIES} ssl crypto Threads::Threads)
    add_library(zerotouch::gnmi ALIAS zerotouch_gnmi)
endif()

# ── tests ───────────────────────────────────────────────────────────────────
if(ZT_BUILD_TESTS)
    # Prefer a system GTest; otherwise fetch a pinned copy so the suite builds
    # anywhere with no host prerequisites.
    find_package(GTest QUIET)
    if(NOT TARGET GTest::gtest_main)
        include(FetchContent)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2
            GIT_SHALLOW    TRUE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    enable_testing()

    add_executable(zerotouch-tests
        test/gnmi_command_test.cpp
        test/gnmi_executor_test.cpp
        test/gnmi_reply_test.cpp
        test/path_policy_test.cpp
        test/bridge_test.cpp)
    target_include_directories(zerotouch-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/test)
    target_link_libraries(zerotouch-tests PRIVATE
        zerotouch_core GTest::gtest GTest::gtest_main)
    target_compile_options(zerotouch-tests PRIVATE -Wall -Wextra)
    add_test(NAME zerotouch-tests COMMAND zerotouch-tests)
endif()

# ── zero-touchd daemon (Phases 4-6; needs ACE + libevent + gnmi_client) ─────
if(ZT_BUILD_DAEMON)
    message(STATUS "zero-touchd build wiring lands in phases 4-6")
    # add_subdirectory(third_party/grace-server/hackthon/app ...)  # gnmi_client
    # add_executable(zero-touchd daemon/main.cpp src/ds_sms_transport.cpp
    #                src/local_gnmi_sink.cpp src/bridge.cpp)
endif()
