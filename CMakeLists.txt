cmake_minimum_required(VERSION 3.21.1)

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS target architecture")
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum supported macOS version")

project(Kode VERSION 1.1.2 LANGUAGES CXX OBJCXX)

execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE KODE_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT KODE_GIT_COMMIT)
    set(KODE_GIT_COMMIT "unknown")
endif()

execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE KODE_DISPLAY_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(KODE_DISPLAY_VERSION)
    string(REGEX REPLACE "^v" "" KODE_DISPLAY_VERSION "${KODE_DISPLAY_VERSION}")
else()
    set(KODE_DISPLAY_VERSION ${PROJECT_VERSION})
endif()

# CFBundleVersion: the tag's version plus a fourth number that orders its
# pre-releases before it (0.2.0-alpha.2 -> 0.2.0.2, -beta.2 -> 0.2.0.102,
# -rc.2 -> 0.2.0.502, 0.2.0 -> 0.2.0.1000).
set(KODE_BUNDLE_VERSION "${PROJECT_VERSION}.1000")
if(KODE_DISPLAY_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-(alpha|beta|rc)\\.([0-9]+))?$")
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
    set(KODE_BUNDLE_VERSION "${release_base}.${release_build}")
endif()

if(APPLE AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/dist" CACHE PATH
        "Directory for the installed macOS application bundle" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Network Quick QuickControls2 QuickDialogs2 Svg)
include(cmake/KSyntaxHighlighting.cmake)

qt_standard_project_setup(REQUIRES 6.12)

include(cmake/AppIcons.cmake)

# The shared Rask modules (git submodule): native window, dialogs, appearance.
set(RASK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RASK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/rask)

# kode-remote (the agent) and kode-askpass, both bundled in Kode.app.
add_subdirectory(remote)

qt_add_executable(kode
    main.cpp
    bin/kode
    ${KODE_APP_ICON_FILES}
)

# The `kode` command (Settings ▸ Files links it into /usr/local/bin).
set_source_files_properties(bin/kode PROPERTIES MACOSX_PACKAGE_LOCATION Resources/bin)

set_source_files_properties(TitleBarButtons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(kode
    URI Kode
    VERSION 1.0
    # For qmllint and qmlcachegen: where rask's modules are built.
    IMPORT_PATH "${CMAKE_CURRENT_BINARY_DIR}/external/rask/qml"
    QML_FILES
        ConnectToServer.qml
        DividerHandle.qml
        DocumentSwitcher.qml
        FindFile.qml
        EditorPane.qml
        BarButton.qml
        EditorWindow.qml
        GoToLine.qml
        Main.qml
        PaneLayout.qml
        SettingsWindow.qml
        Sidebar.qml
        StartPage.qml
        StatusBar.qml
        TextEditor.qml
        TitleBarButtons.qml
    SOURCES
        src/Document.h
        src/Document.cpp
        src/DocumentHistory.h
        src/DocumentHistory.cpp
        src/FileIcons.h
        src/FileIcons.cpp
        src/FileKinds.h
        src/FileKinds.cpp
        src/FileOperations.h
        src/FileOperations.cpp
        src/FileSearch.h
        src/FileSearch.cpp
        src/FileTree.h
        src/FileTree.cpp
        src/FileTypes.h
        src/FileTypes.mm
        src/Preferences.h
        src/Preferences.cpp
        src/Session.h
        src/Session.cpp
        src/Syntaxes.h
        src/Syntaxes.cpp
        src/OpenDocuments.h
        src/OpenDocuments.cpp
        src/Splits.h
        src/Splits.cpp
        src/TextCodec.h
        src/TextCodec.cpp
        src/editor/EditorAccessible.h
        src/editor/EditorAccessible.cpp
        src/editor/Commenting.h
        src/editor/Commenting.cpp
        src/editor/EditorView.h
        src/editor/EditorView.cpp
        src/editor/Folding.h
        src/editor/Folding.cpp
        src/editor/Indentation.h
        src/editor/Indentation.cpp
        src/editor/Matching.h
        src/editor/Matching.cpp
        src/editor/Minimap.h
        src/editor/Minimap.cpp
        src/lsp/Completion.h
        src/lsp/Completion.cpp
        src/lsp/LanguageServer.h
        src/lsp/LanguageServer.cpp
        src/lsp/LanguageServers.h
        src/lsp/LanguageServers.cpp
        src/lsp/Protocol.h
        src/lsp/Protocol.cpp
        src/lsp/SemanticTokens.h
        src/lsp/SemanticTokens.cpp
        remote/protocol/Frame.h
        remote/protocol/Frame.cpp
        remote/protocol/Methods.h
        src/remote/Connection.h
        src/remote/Connection.cpp
        src/remote/Host.h
        src/remote/Host.cpp
        src/remote/Hosts.h
        src/remote/Hosts.cpp
        src/remote/Ssh.h
        src/remote/Ssh.cpp
        src/editor/SemanticStyle.h
        src/editor/SyntaxHighlighter.h
        src/editor/SyntaxHighlighter.cpp
        src/editor/TextBuffer.h
        src/editor/TextBuffer.cpp
        src/editor/TextDocument.h
        src/editor/TextDocument.cpp
        src/editor/WrapIndex.h
        src/editor/WrapIndex.cpp
        src/editor/TextSearch.h
        src/editor/TextSearch.cpp
    RESOURCES
        assets/settings/appearance.svg
        assets/settings/editor.svg
        assets/settings/files.svg
        assets/settings/languageservers.svg
        assets/settings/updates.svg
)

# Kode's highlighting themes (generated by tools/make-syntax-themes.py),
# where SyntaxHighlighter's repository looks for them.
qt_add_resources(kode syntax_themes
    PREFIX "/kode/syntax"
    BASE assets
    FILES
        assets/themes/kode-dark.theme
        assets/themes/kode-light.theme
)

# The generated QML type registrations include the headers by their bare name.
target_include_directories(kode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/editor ${CMAKE_CURRENT_SOURCE_DIR}/src/lsp ${CMAKE_CURRENT_SOURCE_DIR}/src/remote)

# Flag padded class layouts in Debug (the member ordering rule in AGENTS.md),
# on our sources only: Qt's generated code and rask's trip it.
set_property(SOURCE main.cpp src/Document.cpp src/DocumentHistory.cpp src/FileIcons.cpp src/FileKinds.cpp src/FileOperations.cpp src/FileSearch.cpp src/FileTree.cpp src/Preferences.cpp src/Session.cpp src/Syntaxes.cpp src/OpenDocuments.cpp src/Splits.cpp src/TextCodec.cpp src/editor/TextBuffer.cpp src/editor/TextDocument.cpp src/editor/EditorView.cpp src/editor/Commenting.cpp src/editor/Folding.cpp src/editor/EditorAccessible.cpp
    src/editor/SyntaxHighlighter.cpp src/editor/Matching.cpp src/editor/Minimap.cpp src/editor/TextSearch.cpp src/lsp/Completion.cpp src/lsp/LanguageServer.cpp src/lsp/LanguageServers.cpp src/lsp/Protocol.cpp src/lsp/SemanticTokens.cpp src/editor/Indentation.cpp src/editor/WrapIndex.cpp
    remote/protocol/Frame.cpp src/remote/Connection.cpp src/remote/Host.cpp src/remote/Hosts.cpp src/remote/Ssh.cpp
    APPEND PROPERTY COMPILE_OPTIONS $<$<CONFIG:Debug>:-Wpadded>)

# Debug builds get a bundle id of their own, so they don't share macOS
# permissions and Launch Services entries with the installed app.
set(KODE_BUNDLE_ID com.github.rask-project.kode)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    string(APPEND KODE_BUNDLE_ID ".debug")
endif()

# Updates (Sparkle, Rask.Updater). The appcast lists every release; the
# release workflow adds each tag to it (tools/update-appcast.py). Debug
# builds do not update themselves: without a feed URL the updater stays off.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(default_feed_url "")
else()
    set(default_feed_url "https://raw.githubusercontent.com/rask-project/kode/appcast/appcast.xml")
endif()
set(KODE_UPDATE_FEED_URL "${default_feed_url}" CACHE STRING
    "Sparkle appcast URL (empty turns the updater off)")
# The EdDSA public key matching the private key the release workflow signs
# updates with (Sparkle's generate_keys, the SPARKLE_ED_PRIVATE_KEY secret);
# the rask-project apps share it.
set(KODE_UPDATE_PUBLIC_KEY "hb9+LAm6J+NKJX6j3LRQL/G4iV4h1/eJOwS/lYJPDto=" CACHE STRING
    "Sparkle EdDSA public key (empty turns the updater off)")
rask_sparkle_plist_keys(KODE_SPARKLE_PLIST_KEYS FEED_URL "${KODE_UPDATE_FEED_URL}" PUBLIC_KEY "${KODE_UPDATE_PUBLIC_KEY}")

# What the bundle offers the Finder: the document types (Open With, Make
# Default) and "Open in Kode" (cmake/FinderKeys.plist.in). Launch Services
# lists every copy of an app it has seen, so Debug builds leave them out: a
# development build next to the installed app would duplicate them.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(default_finder_integration OFF)
else()
    set(default_finder_integration ON)
endif()
set(KODE_FINDER_INTEGRATION ${default_finder_integration} CACHE BOOL
    "Declare the document types and the Finder Service in the bundle")
set(KODE_FINDER_PLIST_KEYS "")
if(KODE_FINDER_INTEGRATION)
    file(READ cmake/FinderKeys.plist.in KODE_FINDER_PLIST_KEYS)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS cmake/FinderKeys.plist.in)

set_target_properties(kode PROPERTIES
    OUTPUT_NAME "Kode"
    MACOSX_BUNDLE_BUNDLE_NAME "Kode"
    MACOSX_BUNDLE_GUI_IDENTIFIER ${KODE_BUNDLE_ID}
    MACOSX_BUNDLE_BUNDLE_VERSION ${KODE_BUNDLE_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${KODE_DISPLAY_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 Rask Project"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_compile_definitions(kode PRIVATE
    KODE_VERSION="${KODE_DISPLAY_VERSION}"
    KODE_GIT_COMMIT="${KODE_GIT_COMMIT}"
)

target_link_libraries(kode
    PRIVATE
        Qt6::Network
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        # The SVG image plugin, for the Settings icons: deployed with it.
        Qt6::Svg
        KF6::SyntaxHighlighting
        Rask::Application
        Rask::Controls
        Rask::Platform
        Rask::Window
        Rask::Dialogs
        Rask::Updater
        "-framework CoreFoundation"
        "-framework Foundation"
        "-framework UniformTypeIdentifiers"
)
set_source_files_properties(src/FileTypes.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)

# Before the bundle is signed below: its signature covers the libraries.
rask_embed_libraries(kode LIBRARIES KF6::SyntaxHighlighting Rask::Application Rask::Controls Rask::Platform Rask::Window Rask::Dialogs Rask::Updater)

install(TARGETS kode
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# The agents Kode installs on hosts, by <os>-<arch>: this Mac's, built here,
# and those KODE_REMOTE_AGENTS_DIR holds (the Linux ones, built by CI:
# .github/workflows; laid out as linux-x86_64/kode-remote…). And the askpass
# helper ssh runs, beside the app's executable.
set(KODE_REMOTE_AGENTS_DIR "" CACHE PATH "Folder of prebuilt kode-remote agents (<os>-<arch>/kode-remote)")
add_dependencies(kode kode-remote kode-askpass)
add_custom_command(TARGET kode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:kode-askpass>" "$<TARGET_BUNDLE_CONTENT_DIR:kode>/MacOS/kode-askpass"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:kode>/Resources/remote/darwin-arm64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:kode-remote>" "$<TARGET_BUNDLE_CONTENT_DIR:kode>/Resources/remote/darwin-arm64/kode-remote"
    COMMENT "Bundling kode-remote and kode-askpass"
    VERBATIM
)
if(KODE_REMOTE_AGENTS_DIR)
    add_custom_command(TARGET kode POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${KODE_REMOTE_AGENTS_DIR}" "$<TARGET_BUNDLE_CONTENT_DIR:kode>/Resources/remote"
        COMMENT "Bundling the agents in ${KODE_REMOTE_AGENTS_DIR}"
        VERBATIM
    )
endif()

if(APPLE)
    # The linker signs the executable alone, leaving Info.plist unbound;
    # sign the build tree's bundle ad hoc.
    add_custom_command(TARGET kode POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:kode>"
        COMMENT "Signing Kode.app ad hoc"
        VERBATIM
    )

    # A Developer ID for a notarizable release (docs/RELEASE.md); empty: ad hoc.
    set(KODE_CODESIGN_IDENTITY "" CACHE STRING "Developer ID Application identity for the installed bundle")
    set(deploy_tool_options_arg)
    if(KODE_CODESIGN_IDENTITY)
        qt_policy(SET QTP0007 NEW)
        list(APPEND deploy_tool_options_arg "-sign-for-notarization=${KODE_CODESIGN_IDENTITY}")
    endif()

    # The self-contained bundle (Qt, its plugins and QML modules) in
    # ${CMAKE_INSTALL_PREFIX}: `cmake --build … --target package-macos`.
    qt_generate_deploy_qml_app_script(
        TARGET kode
        OUTPUT_SCRIPT deploy_script
        DEPLOY_TOOL_OPTIONS ${deploy_tool_options_arg}
    )
    install(SCRIPT ${deploy_script})
    install(CODE "
        set(APP_BUNDLE \"\${CMAKE_INSTALL_PREFIX}/Kode.app\")
        set(CODESIGN_IDENTITY \"${KODE_CODESIGN_IDENTITY}\")
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/PruneBundle.cmake\")
    ")

    add_custom_target(package-macos
        COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG>
        DEPENDS kode
        COMMENT "Deploying the self-contained Kode bundle to ${CMAKE_INSTALL_PREFIX}"
        VERBATIM
    )
endif()

option(KODE_BUILD_TOOLS "Build the Kode tools (editor benchmark)" ${PROJECT_IS_TOP_LEVEL})
if(KODE_BUILD_TOOLS)
    add_subdirectory(tools/editor-bench)
endif()

option(KODE_BUILD_TESTS "Build the Kode tests" ${PROJECT_IS_TOP_LEVEL})
if(KODE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
