# KSyntaxHighlighting (KDE Frameworks) from the external/syntax-highlighting
# submodule, with ECM from external/extra-cmake-modules: built once at
# configure time, in Release and without its Widgets/Quick parts, into
# ${CMAKE_BINARY_DIR}/deps, then imported as KF6::SyntaxHighlighting (a
# shared library, embedded in the bundle like the rask ones).

set(tekst_deps_prefix "${CMAKE_BINARY_DIR}/deps")
set(tekst_ksh_stamp "${tekst_deps_prefix}/ksyntaxhighlighting.stamp")
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/syntax-highlighting"
    OUTPUT_VARIABLE tekst_ksh_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(tekst_ksh_built "")
if(EXISTS "${tekst_ksh_stamp}")
    file(READ "${tekst_ksh_stamp}" tekst_ksh_built)
endif()

function(tekst_build_dependency name source)
    set(build "${CMAKE_BINARY_DIR}/deps-build/${name}")
    message(STATUS "Building ${name} (once)")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -S "${source}" -B "${build}" -G Ninja
            -DCMAKE_BUILD_TYPE=Release
            "-DCMAKE_INSTALL_PREFIX=${tekst_deps_prefix}"
            "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH};${tekst_deps_prefix}"
            "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            -DBUILD_TESTING=OFF
            -DBUILD_QCH=OFF
            -DBUILD_DOC=OFF
            -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Widgets=ON
            -DCMAKE_DISABLE_FIND_PACKAGE_Qt6PrintSupport=ON
            -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Quick=ON
            -DCMAKE_DISABLE_FIND_PACKAGE_XercesC=ON
            -DCMAKE_DISABLE_FIND_PACKAGE_Python=ON
            ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_QUIET
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Configuring ${name} failed")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} --build "${build}" --target install RESULT_VARIABLE result OUTPUT_QUIET)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Building ${name} failed")
    endif()
endfunction()

if(NOT tekst_ksh_built STREQUAL tekst_ksh_revision)
    tekst_build_dependency(extra-cmake-modules "${CMAKE_CURRENT_SOURCE_DIR}/external/extra-cmake-modules")
    tekst_build_dependency(syntax-highlighting "${CMAKE_CURRENT_SOURCE_DIR}/external/syntax-highlighting")
    file(WRITE "${tekst_ksh_stamp}" "${tekst_ksh_revision}")
endif()

find_package(KF6SyntaxHighlighting REQUIRED CONFIG PATHS "${tekst_deps_prefix}" NO_DEFAULT_PATH)
