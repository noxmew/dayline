cmake_minimum_required(VERSION 3.21)
set(DAYLINE_VERSION "0.1.0" CACHE STRING "Dayline semantic version")
if(NOT DAYLINE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "DAYLINE_VERSION must have the form major.minor.patch")
endif()
project(Dayline VERSION ${DAYLINE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(DAYLINE_BUNDLE_RUNTIME "Install a private runtime from a glibc 2.28 SDK" OFF)
set(DAYLINE_RUNTIME_PREFIX "" CACHE PATH "Private release SDK prefix")
find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Sql DBus Network)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
if(OPENSSL_VERSION VERSION_GREATER_EQUAL "3.0" AND OPENSSL_VERSION VERSION_LESS "4.0")
    set(DAYLINE_RUNTIME_OPENSSL_MAJOR "3")
elseif(OPENSSL_VERSION VERSION_GREATER_EQUAL "1.1.1" AND OPENSSL_VERSION VERSION_LESS "1.2")
    set(DAYLINE_RUNTIME_OPENSSL_MAJOR "1.1")
else()
    message(FATAL_ERROR "Unsupported OpenSSL runtime version: ${OPENSSL_VERSION}")
endif()
find_path(ICAL_INCLUDE_DIR libical/ical.h REQUIRED)
find_library(ICAL_LIBRARY NAMES ical libical.so.3 REQUIRED)
find_path(XCB_INCLUDE_DIR xcb/xcb.h REQUIRED)
find_library(XCB_LIBRARY xcb REQUIRED)
qt_standard_project_setup()

if(DAYLINE_BUNDLE_RUNTIME)
    if(NOT IS_ABSOLUTE "${DAYLINE_RUNTIME_PREFIX}" OR NOT IS_DIRECTORY "${DAYLINE_RUNTIME_PREFIX}")
        message(FATAL_ERROR "DAYLINE_BUNDLE_RUNTIME requires an absolute DAYLINE_RUNTIME_PREFIX")
    endif()
    file(REAL_PATH "${DAYLINE_RUNTIME_PREFIX}" DAYLINE_RUNTIME_PREFIX)
    foreach(runtime_path IN ITEMS "${Qt6Core_DIR}" "${Qt6Quick_DIR}" "${OPENSSL_INCLUDE_DIR}" "${OPENSSL_CRYPTO_LIBRARY}" "${ICAL_INCLUDE_DIR}" "${ICAL_LIBRARY}")
        file(REAL_PATH "${runtime_path}" resolved_runtime_path)
        string(FIND "${resolved_runtime_path}" "${DAYLINE_RUNTIME_PREFIX}/" runtime_prefix_position)
        if(NOT runtime_prefix_position EQUAL 0)
            message(FATAL_ERROR "Release dependency is outside DAYLINE_RUNTIME_PREFIX: ${runtime_path}")
        endif()
    endforeach()
    find_program(DAYLINE_PYTHON_EXECUTABLE python3 REQUIRED)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
endif()

qt_add_executable(dayline src/main.cpp src/calendarstore.cpp src/calendarstore.h
    src/desktopcontroller.cpp src/desktopcontroller.h
    src/secretstore.cpp src/secretstore.h
    src/icalresource.cpp src/icalresource.h
    src/workdaycalendar.cpp src/workdaycalendar.h
    src/icloudsync.cpp src/icloudsync.h)
qt_add_resources(dayline holiday_data
    PREFIX "/dayline"
    FILES assets/holidays/china.json)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(dayline
    URI Dayline
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    SOURCES
        src/windowplacement.cpp
        src/windowplacement.h
    QML_FILES
        qml/Main.qml
        qml/DesktopCalendar.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/ActionButton.qml
        qml/Field.qml
        qml/DateField.qml
        qml/TimeField.qml
        qml/Toggle.qml
        qml/SurfaceMenu.qml
        qml/MenuEntry.qml
        qml/MenuToggle.qml
        qml/MenuRule.qml
        qml/ValueSlider.qml
        qml/DesktopAppearanceMenu.qml
        qml/SelectField.qml
        qml/InlineNotice.qml
        qml/SlimScrollBar.qml
        qml/CalendarCell.qml
        qml/MonthGrid.qml
        qml/EventEditor.qml
        qml/RecurrenceEditor.qml
        qml/EditorGroup.qml
        qml/EditorRow.qml
        qml/MemoPanel.qml
        qml/MemoEditor.qml
        qml/ICloudSettings.qml
)
target_include_directories(dayline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${XCB_INCLUDE_DIR} ${ICAL_INCLUDE_DIR})
target_compile_definitions(dayline PRIVATE DAYLINE_VERSION="${PROJECT_VERSION}")
target_link_libraries(dayline PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Sql Qt6::DBus Qt6::Network OpenSSL::Crypto ${XCB_LIBRARY} ${ICAL_LIBRARY})
if(DAYLINE_BUNDLE_RUNTIME)
    set_target_properties(dayline PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
    install(TARGETS dayline RUNTIME DESTINATION lib/dayline)
    install(PROGRAMS platform/linux/dayline DESTINATION bin)
    install(FILES platform/linux/qt.conf DESTINATION lib/dayline)
    configure_file(cmake/InstallRuntime.cmake.in generated/InstallRuntime.cmake @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generated/InstallRuntime.cmake")
    if(DAYLINE_RUNTIME_OPENSSL_MAJOR STREQUAL "1.1")
        install(FILES scripts/build-runtime-sdk-amd64.sh DESTINATION share/doc/dayline/runtime)
    else()
        install(FILES scripts/build-runtime-sdk.sh DESTINATION share/doc/dayline/runtime)
    endif()
else()
    install(TARGETS dayline RUNTIME DESTINATION bin)
endif()
install(FILES assets/dayline.desktop DESTINATION share/applications)
install(FILES assets/dayline.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION share/doc/dayline RENAME copyright)
install(FILES platform/gnome/metadata.json platform/gnome/extension.js
    DESTINATION share/gnome-shell/extensions/dayline-desktop@dayline)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME dayline)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Dayline")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/noxmew/dayline")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dayline - native desktop calendar")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
# On amd64/arm64, Ubuntu's t64 packages and UOS 25's original package names
# provide the same required SONAMEs. Keep both names with the same ABI floor.
# The release workflow checks the executable's versioned ABI requirements.
if(NOT DAYLINE_BUNDLE_RUNTIME)
# Private packages derive exact dependencies from the installed ELF closure;
# scripts/package-runtime-deb.sh packages them instead of CPack.
set(DAYLINE_DEBIAN_DEPENDENCIES
    "libc6 (>= 2.38)"
    "libgcc-s1"
    "libstdc++6 (>= 12)"
    "libqt6core6t64 (>= 6.4) | libqt6core6 (>= 6.4)"
    "libqt6gui6t64 (>= 6.4) | libqt6gui6 (>= 6.4)"
    "libqt6qml6 (>= 6.4)"
    # QtQuick loads this backing library, which registers QtQml.Models and
    # embeds its qmldir. The separate modelsplugin is optional in Qt >= 6.4.
    "libqt6qmlmodels6 (>= 6.4)"
    "libqt6quick6 (>= 6.4)"
    "libqt6quickcontrols2-6 (>= 6.4)"
    "libqt6sql6t64 (>= 6.4) | libqt6sql6 (>= 6.4)"
    "libqt6dbus6t64 (>= 6.4) | libqt6dbus6 (>= 6.4)"
    "libqt6network6t64 (>= 6.4) | libqt6network6 (>= 6.4)"
    "libssl3t64 (>= 3.0) | libssl3 (>= 3.0)"
    "libical3t64 (>= 3.0) | libical3 (>= 3.0)"
    "libxcb1"
    "libqt6sql6-sqlite"
    "qml6-module-qtqml"
    "qml6-module-qtquick"
    "qml6-module-qtquick-controls"
    "qml6-module-qtquick-layouts"
    "qml6-module-qtquick-window"
    "qml6-module-qtquick-templates"
    "qml6-module-qtqml-workerscript"
)
list(JOIN DAYLINE_DEBIAN_DEPENDENCIES ", " CPACK_DEBIAN_PACKAGE_DEPENDS)
include(CPack)
endif()
