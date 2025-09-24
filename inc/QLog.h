#pragma once

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace QLog
{

// Log severity levels
enum class Level : std::uint8_t
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

// Simple log message structure
struct Message
{
    Level level{};
    std::optional<std::chrono::system_clock::time_point> timestamp; // present if timestamps enabled
    std::string text;
};

// Abstract sink interface (console, file, custom)
class Sink
{
public:
    virtual ~Sink() = default;
    virtual void Write(const Message& message) = 0;
    virtual void Flush() {}
};

// Console sink writes to std::ostream (defaults to std::clog)
class OStreamSink : public Sink
{
public:
    explicit OStreamSink(std::ostream& os);
    void Write(const Message& message) override;
    void Flush() override;
private:
    std::ostream& m_os;
};

// Thread-safe async logger with background thread and non-blocking enqueue
class Logger
{
public:
    // Ctor starts background thread. If capacity==0, queue is unbounded, else drops oldest when full.
    explicit Logger(std::shared_ptr<Sink> sink,
                    Level initialLevel = Level::Info,
                    size_t capacity = 0);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Non-blocking log enqueue; may drop message if below level or queue policy decides
    void Log(Level level, std::string message);

    // Convenience helpers
    void Trace(std::string message)
    {
        Log(Level::Trace, std::move(message));
    }
    void Debug(std::string message)
    {
        Log(Level::Debug, std::move(message));
    }
    void Info(std::string message)
    {
        Log(Level::Info, std::move(message));
    }
    void Warn(std::string message)
    {
        Log(Level::Warn, std::move(message));
    }
    void Error(std::string message)
    {
        Log(Level::Error, std::move(message));
    }
    void Critical(std::string message)
    {
        Log(Level::Critical, std::move(message));
    }

    // Flushes sink after processing current queue
    void Flush();

    // Stop background thread after draining queue
    void Shutdown();

    // Level control
    void SetLevel(Level level);
    Level GetLevel() const
    {
        return m_level.load(std::memory_order_relaxed);
    }

    // Timestamp control (on by default)
    void EnableTimestamps(bool enabled)
    {
        m_timestampsEnabled.store(enabled, std::memory_order_relaxed);
    }
    bool TimestampsEnabled() const
    {
        return m_timestampsEnabled.load(std::memory_order_relaxed);
    }

    // Bounded capacity (0 = unbounded)
    size_t GetCapacity() const
    {
        return m_capacity;
    }

private:
    void Worker();

    std::shared_ptr<Sink> m_sink;

    mutable std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<Message> m_queue;
    const size_t m_capacity;

    std::atomic<Level> m_level;
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_flushRequested{false};
    std::atomic<bool> m_timestampsEnabled{true};

    std::thread m_worker;
};

// Helper to stringify levels
const char* ToString(Level level) noexcept;

// Small stream-style builder that sends to logger on destruction
class LogLine
{
public:
    LogLine(Logger& logger, Level level)
        : m_logger(logger), m_level(level)
    {}
    ~LogLine();

    template <class T>
    LogLine& operator<<(T&& v)
    {
        m_ss << std::forward<T>(v);
        return *this;
    }

private:
    Logger& m_logger;
    Level m_level;
    std::ostringstream m_ss;
};

} // namespace QLog

// Convenience macros for minimal call overhead
#define QLOG_TRACE(logger)  ::QLog::LogLine((logger), ::QLog::Level::Trace)
#define QLOG_DEBUG(logger)  ::QLog::LogLine((logger), ::QLog::Level::Debug)
#define QLOG_INFO(logger)   ::QLog::LogLine((logger), ::QLog::Level::Info)
#define QLOG_WARN(logger)   ::QLog::LogLine((logger), ::QLog::Level::Warn)
#define QLOG_ERROR(logger)  ::QLog::LogLine((logger), ::QLog::Level::Error)
#define QLOG_CRITICAL(logger) ::QLog::LogLine((logger), ::QLog::Level::Critical)
