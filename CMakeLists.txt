cmake_minimum_required(VERSION 3.21)
set(DAYLINE_VERSION "0.1.0" CACHE STRING "Dayline semantic version")
if(NOT DAYLINE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "DAYLINE_VERSION must have the form major.minor.patch")
endif()
project(Dayline VERSION ${DAYLINE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Sql DBus Network)
find_path(ICAL_INCLUDE_DIR libical/ical.h REQUIRED)
find_library(ICAL_LIBRARY NAMES ical libical.so.3 REQUIRED)
find_path(XCB_INCLUDE_DIR xcb/xcb.h REQUIRED)
find_library(XCB_LIBRARY xcb REQUIRED)
qt_standard_project_setup()

string(HEX "$ENV{DAYLINE_UPDATE_PROXY_BASE}" DAYLINE_UPDATE_PROXY_HEX)
configure_file(src/updateconfig.h.in generated/updateconfig.h @ONLY)
unset(DAYLINE_UPDATE_PROXY_HEX)

qt_add_executable(dayline src/main.cpp src/calendarstore.cpp src/calendarstore.h
    src/desktopcontroller.cpp src/desktopcontroller.h
    src/secretstore.cpp src/secretstore.h
    src/icalresource.cpp src/icalresource.h
    src/icloudsync.cpp src/icloudsync.h
    src/updatechecker.cpp src/updatechecker.h)
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
        qml/ICloudSettings.qml
        qml/UpdateSettings.qml
)
target_include_directories(dayline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/generated ${XCB_INCLUDE_DIR} ${ICAL_INCLUDE_DIR})
target_compile_definitions(dayline PRIVATE DAYLINE_VERSION="${PROJECT_VERSION}")
target_link_libraries(dayline PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Sql Qt6::DBus Qt6::Network ${XCB_LIBRARY} ${ICAL_LIBRARY})
install(TARGETS dayline RUNTIME DESTINATION bin)
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
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dayline - a calm, native desktop calendar")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.38), libgcc-s1, libstdc++6 (>= 13.1), libqt6core6t64 (>= 6.4), libqt6gui6t64 (>= 6.4), libqt6qml6 (>= 6.4), libqt6quick6 (>= 6.4), libqt6quickcontrols2-6 (>= 6.4), libqt6sql6t64 (>= 6.4), libqt6dbus6t64 (>= 6.4), libqt6network6t64 (>= 6.4), libical3t64 (>= 3.0), libxcb1, libqt6sql6-sqlite, qt6-qpa-plugins, qml6-module-qtqml, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript, qml6-module-qtqml-models")
include(CPack)
