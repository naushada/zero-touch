cmake_minimum_required(VERSION 3.16.3)
project(zero-touch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ZT_BUILD_TESTS  "Build the zero-touch gtest suite"        ON)
option(ZT_BUILD_SIM    "Build zerotouch-sim, the offline SMS simulator"       ON)
option(ZT_BUILD_GNMI   "Build LocalGnmiSink (needs protobuf/libevent/nghttp2)" OFF)
option(ZT_BUILD_DS     "Build DsSmsTransport (needs ACE + datastore_client)"   OFF)
option(ZT_BUILD_DAEMON "Build the zero-touchd SMS→gNMI daemon"   OFF)
option(ZT_INSTALL_SYSV "Install the SysV init script instead of/alongside systemd" OFF)

# The daemon needs the real gNMI backend and the ds SMS transport.
if(ZT_BUILD_DAEMON)
    set(ZT_BUILD_GNMI ON)
    set(ZT_BUILD_DS   ON)
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
    src/sms_util.cpp
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

# ── zerotouch_ds: DsSmsTransport over the cellular-client ds envelope ────────
# Needs ACE + datastore_client (reused from the iot submodule), so opt-in.
if(ZT_BUILD_DS)
    if(NOT DEFINED ACE_ROOT)
        set(ACE_ROOT /usr/local/ACE_TAO-7.0.0)
    endif()
    if(NOT TARGET datastore_client)
        add_subdirectory(
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iot/modules/data-store
            ${CMAKE_BINARY_DIR}/data-store EXCLUDE_FROM_ALL)
    endif()

    add_library(zerotouch_ds STATIC src/ds_sms_transport.cpp)
    target_include_directories(zerotouch_ds PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc)
    target_include_directories(zerotouch_ds PRIVATE ${ACE_ROOT}/include)
    target_link_directories(zerotouch_ds PUBLIC ${ACE_ROOT}/lib)
    target_link_libraries(zerotouch_ds PUBLIC
        zerotouch_core datastore_client ACE pthread)
    target_compile_options(zerotouch_ds PRIVATE -Wall -Wextra)
    add_library(zerotouch::ds ALIAS zerotouch_ds)
endif()

# ── zerotouch-sim: offline SMS simulator (host tool, no modem/ds/gRPC) ───────
# Uses the real smsctl engine (needs smsctl::core + iot's json single_include)
# with in-memory ds/gNMI backends. Skips gracefully if the json submodule is not
# initialised, so the pure test build never breaks.
if(ZT_BUILD_SIM)
    set(ZT_SIM_JSON
        ${IOT_SMSCTL_DIR}/../../apps/3rdparty/json/single_include/nlohmann/json.hpp)
    if(TARGET smsctl_core AND EXISTS ${ZT_SIM_JSON})
        add_executable(zerotouch-sim sim/zt_sim.cpp)
        target_include_directories(zerotouch-sim PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/inc ${IOT_SMSCTL_DIR}/inc)
        target_link_libraries(zerotouch-sim PRIVATE zerotouch_core smsctl_core)
        target_compile_options(zerotouch-sim PRIVATE -Wall -Wextra)
    else()
        message(STATUS "zerotouch-sim skipped — run: "
                       "git submodule update --init --recursive")
    endif()
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
        test/sms_util_test.cpp
        test/bridge_test.cpp)
    target_include_directories(zerotouch-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/test)
    target_link_libraries(zerotouch-tests PRIVATE
        zerotouch_core GTest::gtest GTest::gtest_main)
    target_compile_options(zerotouch-tests PRIVATE -Wall -Wextra)
    add_test(NAME zerotouch-tests COMMAND zerotouch-tests)
endif()

# ── zero-touchd daemon ──────────────────────────────────────────────────────
# Composes both seams: zerotouch_ds (SMS in/out) + zerotouch_gnmi (LocalGnmiSink)
# + smsctl::core (classic engine + tokenize/session) over one ds Client + reactor.
# ZT_BUILD_DAEMON implied ZT_BUILD_GNMI and ZT_BUILD_DS above, so those targets
# and their deps (protobuf/libevent/nghttp2, ACE/datastore) already exist.
if(ZT_BUILD_DAEMON)
    if(NOT DEFINED ACE_ROOT)
        set(ACE_ROOT /usr/local/ACE_TAO-7.0.0)
    endif()
    # nlohmann/json for lookup_account (auth.users.accounts) — from the iot
    # submodule (needs recursive submodule init).
    set(ZT_JSON_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iot/apps/3rdparty/json/single_include)

    add_executable(zero-touchd
        daemon/main.cpp
        daemon/zerotouchd.cpp)
    target_include_directories(zero-touchd PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/daemon
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${IOT_SMSCTL_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iot/modules/data-store/inc
        ${ZT_JSON_INC}
        ${ACE_ROOT}/include)
    target_link_directories(zero-touchd PRIVATE ${ACE_ROOT}/lib)
    target_link_libraries(zero-touchd PRIVATE
        zerotouch_core zerotouch_gnmi zerotouch_ds
        smsctl_core datastore_client ACE pthread)
    target_compile_options(zero-touchd PRIVATE -Wall -Wextra)

    include(GNUInstallDirs)
    # systemd unit dir (override with -DZT_SYSTEMD_DIR=… for the target sysroot).
    if(NOT DEFINED ZT_SYSTEMD_DIR)
        set(ZT_SYSTEMD_DIR /lib/systemd/system)
    endif()

    install(TARGETS zero-touchd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # ds schema — ds-server auto-loads everything under /etc/iot/ds-schemas.
    install(FILES schemas/zerotouch.lua
            DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/iot/ds-schemas)
    # env file + systemd unit.
    install(FILES packaging/etc-iot/zerotouchd.env
            DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/iot)
    install(FILES packaging/systemd/zero-touchd.service
            DESTINATION ${ZT_SYSTEMD_DIR})
    # SysV init script for boards without systemd (opt-in).
    if(ZT_INSTALL_SYSV)
        install(PROGRAMS packaging/sysv/zero-touchd
                DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/init.d)
    endif()
    # DEPLOY doc referenced by the unit's Documentation= line.
    install(FILES DEPLOY.md
            DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/doc/zero-touch)
endif()
