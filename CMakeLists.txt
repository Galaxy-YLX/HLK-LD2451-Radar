cmake_minimum_required(VERSION 3.21)
project(LD2451Viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 核心库：协议解析 + 组帧 + 命令构造（纯 C++，无 Qt 依赖，便于独立单测）
# ---------------------------------------------------------------------------
add_library(ld2451_core STATIC
    src/protocol/parser.cpp
    src/protocol/frame_assembler.cpp
)
target_include_directories(ld2451_core PUBLIC src)

# ---------------------------------------------------------------------------
# 单元测试（用协议文档第 7 节的实例帧做基准）
# ---------------------------------------------------------------------------
enable_testing()
add_executable(parser_tests tests/parser_test.cpp)
target_link_libraries(parser_tests PRIVATE ld2451_core)
add_test(NAME parser_tests COMMAND parser_tests)

# ---------------------------------------------------------------------------
# Qt 上位机应用
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort)
qt_standard_project_setup()

qt_add_executable(ld2451_viewer
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/transport/serial_transport.cpp
    src/transport/serial_transport.h
    src/ui/radar_view.cpp
    src/ui/radar_view.h
)
target_link_libraries(ld2451_viewer PRIVATE Qt6::Core Qt6::Widgets Qt6::SerialPort ld2451_core)
