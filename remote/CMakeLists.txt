# kode-remote, the agent Kode runs on a host over SSH (docs/REMOTE.md): the
# standard library, POSIX and yyjson; no Qt. Static on Linux.
enable_language(C)

add_library(yyjson STATIC ${PROJECT_SOURCE_DIR}/external/yyjson/src/yyjson.c)
target_include_directories(yyjson SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/external/yyjson/src)

add_executable(kode-remote
    protocol/Frame.h
    protocol/Frame.cpp
    protocol/Methods.h
    agent/Agent.h
    agent/Agent.cpp
    agent/Bridge.h
    agent/Bridge.cpp
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
