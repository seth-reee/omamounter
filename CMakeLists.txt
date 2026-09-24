cmake_minimum_required(VERSION 3.24)
project(Tether VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test Concurrent)

add_library(tether_core
    cpp/filetransaction.cpp
    cpp/mountidentity.cpp
    cpp/config.cpp
    cpp/config.h
    cpp/backends.cpp
    cpp/backends.h
    cpp/systemdclient.cpp
    cpp/systemdclient.h
    cpp/theme.cpp
    cpp/theme.h)
target_link_libraries(tether_core PUBLIC Qt6::Core Qt6::Network Qt6::Widgets)
target_include_directories(tether_core PUBLIC cpp)
target_compile_definitions(tether_core PUBLIC TETHER_VERSION="${PROJECT_VERSION}")
qt_add_resources(tether_core "tether_license" PREFIX "/" FILES LICENSE)

add_executable(tether
    cpp/main.cpp
    cpp/mainwindow.cpp
    cpp/mainwindow.h
    cpp/settingsdialog.cpp
    cpp/settingsdialog.h)
qt_add_resources(tether "tether_resources"
    PREFIX "/"
    FILES resources/tether.png
)
target_link_libraries(tether PRIVATE tether_core Qt6::Concurrent)

foreach(helper IN ITEMS tether-apply-helper tether-control-helper)
    add_executable(${helper} cpp/helper.cpp cpp/config.cpp cpp/mountidentity.cpp cpp/filetransaction.cpp)
    target_link_libraries(${helper} PRIVATE Qt6::Core)
    target_include_directories(${helper} PRIVATE cpp)
endforeach()
target_compile_definitions(tether-apply-helper PRIVATE TETHER_APPLY_HELPER)
target_compile_definitions(tether-control-helper PRIVATE TETHER_CONTROL_HELPER)

enable_testing()
add_executable(helper_integration tests_cpp/helper_integration.cpp cpp/config.cpp cpp/mountidentity.cpp cpp/filetransaction.cpp)
target_include_directories(helper_integration PRIVATE cpp)
target_link_libraries(helper_integration PRIVATE Qt6::Core Qt6::Test)
add_test(NAME helper_integration COMMAND helper_integration)
add_executable(tether_tests tests_cpp/tests.cpp cpp/settingsdialog.cpp cpp/settingsdialog.h cpp/mainwindow.cpp cpp/mainwindow.h)
target_link_libraries(tether_tests PRIVATE tether_core Qt6::Test Qt6::Concurrent)
add_test(NAME tether_tests COMMAND tether_tests)
set_tests_properties(tether_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

install(TARGETS tether RUNTIME DESTINATION bin)
install(TARGETS tether-apply-helper tether-control-helper RUNTIME DESTINATION lib/tether)
install(FILES packaging/tether.desktop DESTINATION share/applications)
install(FILES resources/tether.png DESTINATION share/icons/hicolor/512x512/apps)
install(FILES packaging/org.tether.policy DESTINATION share/polkit-1/actions)
install(FILES LICENSE DESTINATION share/licenses/tether)
