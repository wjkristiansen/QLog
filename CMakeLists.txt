cmake_minimum_required(VERSION 3.20)

project(QLog LANGUAGES CXX VERSION 0.1.0)

# Options
option(QLOG_BUILD_TESTS "Build QLog unit tests" ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Library target
add_library(QLog
    src/QLog.cpp
)
add_library(QLog::QLog ALIAS QLog)

# Include directories
target_include_directories(QLog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

# Compile definitions and warnings
if (MSVC)
    target_compile_options(QLog PRIVATE /W4 /permissive-)
else()
    target_compile_options(QLog PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Threads (for background logging thread)
find_package(Threads REQUIRED)

target_link_libraries(QLog PRIVATE Threads::Threads)

# Tests and samples
if (QLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(samples)
endif()
