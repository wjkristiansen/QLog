#include <gtest/gtest.h>
#include "QLog.h"

#include <chrono>
#include <sstream>
#include <thread>

// Avoid using-directives; use fully qualified std::chrono types

TEST(QLog, BasicWriteAndFlush)
{
    std::ostringstream oss;
    QLog::OStreamSink sink(oss);
    QLog::Logger logger{sink, QLog::Level::Trace};

    logger.Info("hello");
    logger.Warn("world");
    logger.Flush();

    // Give worker some time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto s = oss.str();
    // Expect a timestamp prefix like: [YYYY-mm-dd HH:MM:SS.uuuuuu]
    ASSERT_FALSE(s.empty());
    EXPECT_EQ(s.front(), '[');
    EXPECT_NE(s.find("] INFO: hello"), std::string::npos);
    EXPECT_NE(s.find("] WARN: world"), std::string::npos);
}

TEST(QLog, LevelFiltering)
{
    std::ostringstream oss;
    QLog::OStreamSink sink(oss);
    QLog::Logger logger{sink, QLog::Level::Warn};

    logger.Info("won't show");
    logger.Error("shows");
    logger.Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto s = oss.str();
    EXPECT_EQ(s.find("] INFO: "), std::string::npos);
    EXPECT_NE(s.find("] ERROR: shows"), std::string::npos);
}

TEST(QLog, BoundedCapacityDropsOldest)
{
    std::ostringstream oss;
    QLog::OStreamSink sink(oss);
    QLog::Logger logger{sink, QLog::Level::Trace, 3};

    logger.Info("a");
    logger.Info("b");
    logger.Info("c");
    logger.Info("d"); // should drop 'a'

    logger.Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto s = oss.str();

    EXPECT_EQ(s.find("] INFO: a"), std::string::npos);
    EXPECT_NE(s.find("] INFO: b"), std::string::npos);
    EXPECT_NE(s.find("] INFO: c"), std::string::npos);
    EXPECT_NE(s.find("] INFO: d"), std::string::npos);
}

TEST(QLog, TimestampsCanBeDisabled)
{
    std::ostringstream oss;
    QLog::OStreamSink sink(oss);
    QLog::Logger logger{sink, QLog::Level::Trace};

    logger.EnableTimestamps(false);

    logger.Info("no ts 1");
    logger.Warn("no ts 2");
    logger.Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto s = oss.str();
    // Should not start with '[' when timestamps are disabled
    ASSERT_FALSE(s.empty());
    EXPECT_NE(s.find("INFO: no ts 1"), std::string::npos);
    EXPECT_NE(s.find("WARN: no ts 2"), std::string::npos);
    // Check that at least the first line does not start with '['
    auto firstNewline = s.find('\n');
    auto firstLine = s.substr(0, firstNewline);
    EXPECT_NE(firstLine.find('['), 0u);
}

TEST(QLog, PrintfStyleFormattingAndFiltering)
{
    std::ostringstream oss;
    QLog::OStreamSink sink(oss);
    QLog::Logger logger{sink, QLog::Level::Info};

    // Test basic formatting
    logger.Info("User %s logged in with ID %d", "alice", 123);
    logger.Warn("Processing %d items at %.2f MB/s", 42, 15.75);
    
    // Test level filtering - this should not appear since Debug < Info
    logger.Debug("This debug message with %s should be filtered", "args");
    
    logger.Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto s = oss.str();
    EXPECT_NE(s.find("] INFO: User alice logged in with ID 123"), std::string::npos);
    EXPECT_NE(s.find("] WARN: Processing 42 items at 15.75 MB/s"), std::string::npos);
    EXPECT_EQ(s.find("DEBUG"), std::string::npos); // Debug message should be filtered
}

TEST(QLog, BreaksCanThrowForTesting)
{
    std::ostringstream oss;
    QLog::OStreamSink sink(oss);
    QLog::Logger logger{sink, QLog::Level::Trace};

    logger.SetBreakLevel(QLog::Level::Error);
    logger.SetBreakMode(QLog::BreakMode::Throw);
    logger.EnableBreaks(true);

    // Below threshold: no throw
    EXPECT_NO_THROW(logger.Warn("warn no break"));
    // At/above threshold: throws BreakException
    EXPECT_THROW(logger.Error("boom"), QLog::BreakException);

    // Ensure normal logging still works after exception
    logger.EnableBreaks(false);
    logger.Error("after");
    logger.Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto s = oss.str();
    EXPECT_NE(s.find("ERROR: after"), std::string::npos);
}

TEST(QLog, FormatTimestamp)
{
    using namespace std::chrono;

    // Test with no timestamp
    QLog::Message msg1{QLog::Level::Info, std::nullopt, "test"};
    EXPECT_EQ(QLog::FormatTimestamp(msg1), "");

    // Test with timestamp (Sep 27, 2024 07:10:15.123456)
    auto tp = system_clock::time_point{seconds{1727446215} + microseconds{123456}};
    QLog::Message msg2{QLog::Level::Info, tp, "test"};
    
    auto formatted = QLog::FormatTimestamp(msg2);
    
    // Should contain expected components
    EXPECT_NE(formatted.find("["), std::string::npos);  // Opening bracket
    EXPECT_NE(formatted.find("]"), std::string::npos);  // Closing bracket
    EXPECT_NE(formatted.find("2024-09-27"), std::string::npos);  // Date
    EXPECT_NE(formatted.find("07:10:15"), std::string::npos);   // Time
    EXPECT_NE(formatted.find(".123456"), std::string::npos);    // Microseconds
    EXPECT_EQ(formatted.back(), ' ');  // Ends with space
    
    // Should have expected format: "[2024-09-27 07:10:15.123456] " (29 chars)
    EXPECT_EQ(formatted.length(), 29u);
    EXPECT_EQ(formatted, "[2024-09-27 07:10:15.123456] ");
}