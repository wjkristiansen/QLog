include(FetchContent)

# Fetch GoogleTest
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE  # Avoid CMP0135 warning: use extraction time for fetched archive
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_disable_pthreads OFF)
FetchContent_MakeAvailable(googletest)

add_executable(qlog_tests
    QLogTests.cpp
)

target_link_libraries(qlog_tests
    PRIVATE
        QLog::QLog
        GTest::gtest_main
)

# Ensure the public headers are visible even when usage requirements aren't propagated
target_include_directories(qlog_tests PRIVATE ${CMAKE_SOURCE_DIR}/inc)

if (MSVC)
    target_compile_options(qlog_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(qlog_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(qlog_tests)
