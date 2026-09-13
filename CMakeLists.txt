cmake_minimum_required(VERSION 3.24)

project(SmartScheduler
    VERSION 1.0.4
    DESCRIPTION "智排课系统 —— 基于 Qt 6 的现代化自动排课系统"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Quick
    QuickControls2
    Sql
    PrintSupport
    Test
)

qt_standard_project_setup(REQUIRES 6.8)

# QXlsx：Excel 读写第三方库（静态，v1.5.0 源码内嵌）
add_subdirectory(3rdparty/QXlsx/QXlsx)

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
