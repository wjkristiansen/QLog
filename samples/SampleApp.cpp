#include "QLog.h"
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    QLog::OStreamSink sink(std::clog);
    QLog::Logger logger{sink, QLog::Level::Trace};

    logger.Info("QLog sample app starting...");

    // Log with printf-style API
    logger.Info("pi=%.5f", 3.14159);

    // Demonstrate timestamp toggle
    logger.EnableTimestamps(false);
    logger.Info("timestamps disabled");
    logger.EnableTimestamps(true);
    logger.Info("timestamps enabled again");

    // Spawn a few threads to produce logs concurrently
    std::vector<std::thread> threads;
    threads.reserve(3);
    for (int i = 0; i < 3; ++i)
    {
        threads.emplace_back([i, &logger]()
        {
            for (int n = 0; n < 5; ++n)
            {
                logger.Debug("worker %d: message %d", i, n);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    // Join threads explicitly
    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }

    // Flush and shutdown
    logger.Flush();
    logger.Shutdown();

    return 0;
}
