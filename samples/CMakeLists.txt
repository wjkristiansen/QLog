add_executable(qlog_sample
    SampleApp.cpp
)

target_link_libraries(qlog_sample PRIVATE QLog::QLog)

# Ensure headers are found in this subdir build
target_include_directories(qlog_sample PRIVATE ${CMAKE_SOURCE_DIR}/inc)

if (MSVC)
    target_compile_options(qlog_sample PRIVATE /W4 /permissive-)
else()
    target_compile_options(qlog_sample PRIVATE -Wall -Wextra -Wpedantic)
endif()
