cmake_minimum_required(VERSION 3.21.1)

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS target architecture")
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum supported macOS version")

project(Tekst VERSION 0.1.0 LANGUAGES CXX OBJCXX)

execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE TEKST_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT TEKST_GIT_COMMIT)
    set(TEKST_GIT_COMMIT "unknown")
endif()

execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE TEKST_DISPLAY_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(TEKST_DISPLAY_VERSION)
    string(REGEX REPLACE "^v" "" TEKST_DISPLAY_VERSION "${TEKST_DISPLAY_VERSION}")
else()
    set(TEKST_DISPLAY_VERSION ${PROJECT_VERSION})
endif()

# CFBundleVersion: the tag's version plus a fourth number that orders its
# pre-releases before it (0.2.0-alpha.2 -> 0.2.0.2, -beta.2 -> 0.2.0.102,
# -rc.2 -> 0.2.0.502, 0.2.0 -> 0.2.0.1000).
set(TEKST_BUNDLE_VERSION "${PROJECT_VERSION}.1000")
if(TEKST_DISPLAY_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-(alpha|beta|rc)\\.([0-9]+))?$")
    set(release_base "${CMAKE_MATCH_1}")
    set(prerelease_kind "${CMAKE_MATCH_3}")
    set(prerelease_number "${CMAKE_MATCH_4}")
    if(prerelease_kind STREQUAL "alpha")
        set(release_build "${prerelease_number}")
    elseif(prerelease_kind STREQUAL "beta")
        math(EXPR release_build "100 + ${prerelease_number}")
    elseif(prerelease_kind STREQUAL "rc")
        math(EXPR release_build "500 + ${prerelease_number}")
    else()
        set(release_build 1000)
    endif()
    set(TEKST_BUNDLE_VERSION "${release_base}.${release_build}")
endif()

if(APPLE AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/dist" CACHE PATH
        "Directory for the installed macOS application bundle" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.12)

# Flag padded/misaligned class layouts in Debug (see the member ordering rule
# in AGENTS.md).
add_compile_options($<$<CONFIG:Debug>:-Wpadded>)

include(cmake/AppIcons.cmake)

qt_add_executable(tekst
    main.cpp
    ${TEKST_APP_ICON_FILES}
)

qt_add_qml_module(tekst
    URI Tekst
    VERSION 1.0
    QML_FILES
        Main.qml
)

# Debug builds get a bundle id of their own, so they don't share macOS
# permissions and Launch Services entries with the installed app.
set(TEKST_BUNDLE_ID com.github.rask-project.tekst)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    string(APPEND TEKST_BUNDLE_ID ".debug")
endif()

set_target_properties(tekst PROPERTIES
    OUTPUT_NAME "Tekst"
    MACOSX_BUNDLE_BUNDLE_NAME "Tekst"
    MACOSX_BUNDLE_GUI_IDENTIFIER ${TEKST_BUNDLE_ID}
    MACOSX_BUNDLE_BUNDLE_VERSION ${TEKST_BUNDLE_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${TEKST_DISPLAY_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 Rask Project"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_compile_definitions(tekst PRIVATE
    TEKST_VERSION="${TEKST_DISPLAY_VERSION}"
    TEKST_GIT_COMMIT="${TEKST_GIT_COMMIT}"
)

target_link_libraries(tekst
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
)

install(TARGETS tekst
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(APPLE)
    # The linker signs the executable alone, leaving Info.plist unbound;
    # sign the build tree's bundle ad hoc.
    add_custom_command(TARGET tekst POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:tekst>"
        COMMENT "Signing Tekst.app ad hoc"
        VERBATIM
    )

    qt_generate_deploy_qml_app_script(
        TARGET tekst
        OUTPUT_SCRIPT deploy_script
    )
    install(SCRIPT ${deploy_script})
endif()
