# QLog

A low-overhead, cross-platform, asynchronous logging library in C++ (C++20) using a background worker thread and STL. Built with CMake and tested with GoogleTest.

## Goals
- Non-blocking log enqueue on the caller thread
- Background thread writes to sinks (console, file, custom)
- Level filtering and minimal overhead when disabled
- Cross-platform; uses only C++ STL and threads
- Default microsecond-resolution timestamps in output, e.g. `[2025-09-23 12:34:56.123456] INFO: message`

## Layout
- `inc/QLog.h` — public API
- `src/QLog.cpp` — implementation
- `tests/` — unit tests (GoogleTest via FetchContent)

## Build

Using CMake on Windows PowerShell:

```pwsh
# from repo root
$buildDir = "build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
cmake -S . -B $buildDir -DQLOG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build $buildDir --config Release
ctest --test-dir $buildDir -C Release --output-on-failure
```

## Usage

```cpp
#include "QLog.h"
#include <iostream>

int main() {
    auto sink = std::make_shared<QLog::OStreamSink>(std::clog); // or custom sink
    QLog::Logger logger{sink, QLog::Level::Info};

    logger.Info("Hello QLog");
    QLOG_DEBUG(logger) << "pi=" << 3.14159;

    logger.Flush(); // request flush
    logger.Shutdown(); // optional (called from dtor)

    // Disable timestamps if desired
    // logger.EnableTimestamps(false);
}
```

## Extending
- Implement your own `QLog::Sink` to send messages to files, rotating logs, etc.
- Consider batching writes or using lock-free queues for even lower latency.
