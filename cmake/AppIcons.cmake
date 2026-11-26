# App icon. macOS 26 and later draw an app's icon from an Icon Composer
# document (assets/kode.icon) compiled into Assets.car (CFBundleIconName); a
# plain .icns whose shape is not the system's rounded square is shrunk onto
# a gray tile. actool compiles the .icon into an Assets.car plus a .icns that
# older macOS versions read (CFBundleIconFile).
#
# Without actool (Command Line Tools only) the bundle keeps the legacy
# assets/kode.icns (tools/make-app-icons.py).
#
# Sets KODE_APP_ICON_FILES (icon resources, sources of the kode target),
# MACOSX_BUNDLE_ICON_FILE and KODE_ICON_NAME_KEY (the CFBundleIconName plist
# entry, empty without actool).

execute_process(
    COMMAND xcrun --find actool
    OUTPUT_VARIABLE kode_actool
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE kode_actool_result)

# The Debug kit may leave the deployment target empty; actool needs one.
set(kode_icon_target "${CMAKE_OSX_DEPLOYMENT_TARGET}")
if(NOT kode_icon_target)
    set(kode_icon_target 14.4)
endif()

if(kode_actool_result EQUAL 0 AND kode_actool)
    set(icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/kode.icon")
    set(out_dir "${CMAKE_BINARY_DIR}/icons/kode")
    file(GLOB icon_inputs CONFIGURE_DEPENDS "${icon}/icon.json" "${icon}/Assets/*")
    set(KODE_APP_ICON_FILES "${out_dir}/Assets.car" "${out_dir}/kode.icns")
    add_custom_command(
        OUTPUT ${KODE_APP_ICON_FILES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
        COMMAND xcrun actool "${icon}" --compile "${out_dir}"
            --platform macosx --target-device mac
            --minimum-deployment-target ${kode_icon_target}
            --app-icon kode --include-all-app-icons
            --output-partial-info-plist "${out_dir}/partial.plist"
            --errors --warnings
        DEPENDS ${icon_inputs}
        COMMENT "Compiling kode.icon"
        VERBATIM)
    set(KODE_ICON_NAME_KEY "<key>CFBundleIconName</key>\n\t<string>kode</string>")
else()
    message(STATUS "actool not found (Xcode): using the legacy .icns app icon")
    set(KODE_APP_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/kode.icns")
    set(KODE_ICON_NAME_KEY "")
endif()
set(MACOSX_BUNDLE_ICON_FILE kode.icns)
set_source_files_properties(${KODE_APP_ICON_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
