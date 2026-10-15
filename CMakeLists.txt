cmake_minimum_required(VERSION 3.21)
project(Dayline VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Sql DBus)
find_path(XCB_INCLUDE_DIR xcb/xcb.h REQUIRED)
find_library(XCB_LIBRARY xcb REQUIRED)
qt_standard_project_setup()

qt_add_executable(dayline src/main.cpp src/calendarstore.cpp src/calendarstore.h
    src/desktopcontroller.cpp src/desktopcontroller.h)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(dayline
    URI Dayline
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/DesktopCalendar.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/ActionButton.qml
        qml/Field.qml
        qml/Toggle.qml
        qml/CalendarCell.qml
        qml/EventEditor.qml
)
target_include_directories(dayline PRIVATE ${XCB_INCLUDE_DIR})
target_link_libraries(dayline PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Sql Qt6::DBus ${XCB_LIBRARY})
install(TARGETS dayline RUNTIME DESTINATION bin)
install(FILES assets/dayline.desktop DESTINATION share/applications)
install(FILES assets/dayline.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES platform/gnome/metadata.json platform/gnome/extension.js
    DESTINATION share/gnome-shell/extensions/dayline-desktop@dayline)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME dayline)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Dayline")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dayline - a calm, native desktop calendar")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.38), libgcc-s1, libstdc++6 (>= 13.1), libqt6core6t64 (>= 6.4), libqt6gui6t64 (>= 6.4), libqt6qml6 (>= 6.4), libqt6quick6 (>= 6.4), libqt6quickcontrols2-6 (>= 6.4), libqt6sql6t64 (>= 6.4), libqt6sql6-sqlite, qt6-qpa-plugins, qml6-module-qtqml, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript, qml6-module-qtqml-models")
string(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS ", libxcb1, libqt6dbus6t64 (>= 6.4)")
include(CPack)
