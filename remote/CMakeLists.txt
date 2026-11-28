# kode-remote, the agent Kode runs on a host over SSH (docs/REMOTE.md): the
# standard library, POSIX and yyjson; no Qt. Static on Linux.
#
# Also a project of its own, for the hosts' agents (no Qt there):
#   cmake -S remote -B build/remote -DKODE_DISPLAY_VERSION=1.2.0
# tools/build-remote.sh does it in CI's Alpine containers.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21.1)
    project(KodeRemote LANGUAGES C CXX)
    set(KODE_DISPLAY_VERSION "0.0.0" CACHE STRING "The version of the Kode these agents go with")
    option(KODE_REMOTE_FUZZ "Build the fuzzers (clang, libFuzzer)" OFF)
else()
    enable_language(C)
endif()

set(yyjson_dir ${CMAKE_CURRENT_SOURCE_DIR}/../external/yyjson/src)
add_library(yyjson STATIC ${yyjson_dir}/yyjson.c)
target_include_directories(yyjson SYSTEM PUBLIC ${yyjson_dir})

add_executable(kode-remote
    protocol/Frame.h
    protocol/Frame.cpp
    protocol/Methods.h
    agent/Agent.h
    agent/Agent.cpp
    agent/Bridge.h
    agent/Bridge.cpp
    agent/Channel.h
    agent/EventLoop.h
    agent/EventLoop.cpp
    agent/Fd.h
    agent/Json.h
    agent/Json.cpp
    agent/Runtime.h
    agent/Runtime.cpp
    agent/Service.h
    agent/Service.cpp
    agent/Session.h
    agent/Session.cpp
    agent/Sha256.h
    agent/Sha256.cpp
    agent/services/Fs.h
    agent/services/Fs.cpp
    agent/services/Process.h
    agent/services/Process.cpp
    agent/services/Search.h
    agent/services/Search.cpp
    agent/services/Watch.h
    agent/services/Watch.cpp
    agent/services/System.h
    agent/services/System.cpp
    agent/main.cpp
)
target_compile_features(kode-remote PRIVATE cxx_std_23)
target_compile_options(kode-remote PRIVATE -fno-exceptions $<$<CONFIG:Debug>:-Wpadded>)
target_compile_definitions(kode-remote PRIVATE KODE_REMOTE_VERSION="${KODE_DISPLAY_VERSION}")
target_link_libraries(kode-remote PRIVATE yyjson)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(kode-remote PRIVATE -static)
endif()

# ssh's SSH_ASKPASS: asks the running Kode (docs/REMOTE.md, section 7).
add_executable(kode-askpass askpass/main.cpp agent/Fd.h)
target_compile_features(kode-askpass PRIVATE cxx_std_23)
target_compile_options(kode-askpass PRIVATE -fno-exceptions $<$<CONFIG:Debug>:-Wpadded>)

# A stream of frames in random pieces, and their JSON (docs/REMOTE.md, section 7).
if(KODE_REMOTE_FUZZ)
    add_executable(fuzz-frames fuzz/fuzz_frames.cpp protocol/Frame.cpp agent/Json.cpp)
    target_compile_features(fuzz-frames PRIVATE cxx_std_23)
    target_compile_options(fuzz-frames PRIVATE -fsanitize=fuzzer,address,undefined -fno-exceptions)
    target_link_options(fuzz-frames PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_libraries(fuzz-frames PRIVATE yyjson)
endif()
