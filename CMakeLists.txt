cmake_minimum_required(VERSION 3.21)
project(BlockLauncher VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Xml)
qt_standard_project_setup()

qt_add_executable(BlockLauncher WIN32
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    src/Game.cpp src/Game.h
    src/Auth.cpp src/Auth.h
    src/ModLoader.cpp src/ModLoader.h
    src/Mods.cpp src/Mods.h
    src/Skins.cpp src/Skins.h
    src/Config.h
    src/Net.h
    src/Style.h
)

target_link_libraries(BlockLauncher PRIVATE Qt6::Widgets Qt6::Network Qt6::Xml)

if(WIN32)
    target_link_libraries(BlockLauncher PRIVATE dwmapi)
endif()
