cmake_minimum_required(VERSION 3.16)
project(qt_oled_3d CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Gui)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui)

add_executable(qt_oled main.cpp)
target_link_libraries(qt_oled PRIVATE Qt${QT_VERSION_MAJOR}::Gui i2c)