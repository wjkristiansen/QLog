#include "QLog.h"
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    auto sink = std::make_shared<QLog::OStreamSink>(std::clog);
    QLog::Logger logger{sink, QLog::Level::Trace};

    logger.Info("QLog sample app starting...");

    // Log with macros and stream-style API
    QLOG_INFO(logger) << "pi=" << 3.14159;

    // Demonstrate timestamp toggle
    logger.EnableTimestamps(false);
    logger.Info("timestamps disabled");
    logger.EnableTimestamps(true);
    logger.Info("timestamps enabled again");

    // Spawn a few threads to produce logs concurrently
    std::vector<std::jthread> threads;
    for (int i = 0; i < 3; ++i)
    {
        threads.emplace_back([i, &logger]
        {
            for (int n = 0; n < 5; ++n)
            {
                logger.Debug(std::string("worker ") + std::to_string(i) + ": message " + std::to_string(n));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    // Flush and shutdown
    logger.Flush();
    logger.Shutdown();

    return 0;
}
