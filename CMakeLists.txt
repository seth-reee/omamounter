cmake_minimum_required(VERSION 3.24)
project(omamounter VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test)

add_library(omamounter_core
    cpp/config.cpp
    cpp/config.h
    cpp/backends.cpp
    cpp/backends.h
    cpp/systemdclient.cpp
    cpp/systemdclient.h
    cpp/theme.cpp
    cpp/theme.h)
target_link_libraries(omamounter_core PUBLIC Qt6::Core Qt6::Network Qt6::Widgets)
target_include_directories(omamounter_core PUBLIC cpp)

add_executable(omamounter
    cpp/main.cpp
    cpp/mainwindow.cpp
    cpp/mainwindow.h
    cpp/settingsdialog.cpp
    cpp/settingsdialog.h)
target_link_libraries(omamounter PRIVATE omamounter_core)

foreach(helper IN ITEMS omamounter-apply-helper omamounter-control-helper)
    add_executable(${helper} cpp/helper.cpp cpp/config.cpp)
    target_link_libraries(${helper} PRIVATE Qt6::Core)
    target_include_directories(${helper} PRIVATE cpp)
endforeach()
target_compile_definitions(omamounter-apply-helper PRIVATE OMAMOUNTER_APPLY_HELPER)
target_compile_definitions(omamounter-control-helper PRIVATE OMAMOUNTER_CONTROL_HELPER)

enable_testing()
add_executable(omamounter_tests tests_cpp/tests.cpp)
target_link_libraries(omamounter_tests PRIVATE omamounter_core Qt6::Test)
add_test(NAME omamounter_tests COMMAND omamounter_tests)
set_tests_properties(omamounter_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

install(TARGETS omamounter RUNTIME DESTINATION bin)
install(TARGETS omamounter-apply-helper omamounter-control-helper RUNTIME DESTINATION lib/omamounter)
install(FILES packaging/omamounter.desktop DESTINATION share/applications)
install(FILES packaging/org.omamounter.policy DESTINATION share/polkit-1/actions)
install(FILES LICENSE DESTINATION share/licenses/omamounter)
