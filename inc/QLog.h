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
#include <deque>
#include <sstream>
#include <string>
#include <string_view>
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

// How Logger triggers a break when threshold is met
enum class BreakMode : std::uint8_t
{
    DebugBreak, // trigger debugger break (__debugbreak/__builtin_trap)
    Throw       // throw an exception (test-friendly)
};

// Exception thrown when BreakMode::Throw is active and a break is triggered
struct BreakException : public std::exception
{
    const char* what() const noexcept override { return "QLog break triggered"; }
};

// Simple log message structure
struct Message
{
    Level level{};
    std::optional<std::chrono::system_clock::time_point> timestamp; // present if timestamps enabled
    // Text is a non-owning view into memory managed by Logger's internal pool
    std::string_view text{};
    // Storage metadata for releasing memory after sink write
    void* storage{nullptr};
    size_t storageSize{0};
    bool storagePooled{false};
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

    // Optional debug break when logging messages at or above a threshold
    void SetBreakLevel(Level level)
    {
        m_breakLevel.store(level, std::memory_order_relaxed);
    }
    Level GetBreakLevel() const
    {
        return m_breakLevel.load(std::memory_order_relaxed);
    }
    void EnableBreaks(bool enabled)
    {
        m_breakEnabled.store(enabled, std::memory_order_relaxed);
    }
    bool BreaksEnabled() const
    {
        return m_breakEnabled.load(std::memory_order_relaxed);
    }

    void SetBreakMode(BreakMode mode)
    {
        m_breakMode.store(mode, std::memory_order_relaxed);
    }
    BreakMode GetBreakMode() const
    {
        return m_breakMode.load(std::memory_order_relaxed);
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

    // Simple fixed-block buffer pool to minimize heap allocations for message text
    class BufferPool
    {
    public:
        BufferPool(size_t blockSize, size_t blockCount)
            : m_blockSize(blockSize),
              m_storage(blockSize * blockCount),
              m_freeList(blockCount),
              m_freeCount(blockCount)
        {
            for (size_t i = 0; i < blockCount; ++i)
            {
                m_freeList[i] = static_cast<std::uint32_t>(i);
            }
        }

        struct Allocation
        {
            void* ptr{nullptr};
            size_t size{0};
            bool pooled{false};
        };

        Allocation Allocate(size_t n)
        {
            if (n <= m_blockSize && m_freeCount > 0)
            {
                auto idx = m_freeList[--m_freeCount];
                return Allocation{ m_storage.data() + static_cast<ptrdiff_t>(idx) * static_cast<ptrdiff_t>(m_blockSize), m_blockSize, true };
            }
            // Fallback to heap for large messages
            char* p = new char[n];
            return Allocation{ p, n, false };
        }

        void Deallocate(void* p, size_t /*n*/, bool pooled)
        {
            if (!p) return;
            if (pooled)
            {
                char* base = m_storage.data();
                ptrdiff_t diff = static_cast<char*>(p) - base;
                if (diff >= 0 && static_cast<size_t>(diff) < m_storage.size())
                {
                    auto idx = static_cast<std::uint32_t>(diff / static_cast<ptrdiff_t>(m_blockSize));
                    m_freeList[m_freeCount++] = idx;
                }
            }
            else
            {
                delete[] static_cast<char*>(p);
            }
        }

    private:
        size_t m_blockSize{0};
        std::vector<char> m_storage;           // contiguous backing storage
        std::vector<std::uint32_t> m_freeList;  // stack of free block indices
        size_t m_freeCount{0};
    };

    // Helper to release any storage held by a message
    void ReleaseMessageStorage(Message& msg)
    {
        if (msg.storage)
        {
            std::lock_guard<std::mutex> lk(m_poolMtx);
            m_pool.Deallocate(msg.storage, msg.storageSize, msg.storagePooled);
            msg.storage = nullptr;
            msg.storageSize = 0;
            msg.storagePooled = false;
            msg.text = std::string_view{};
        }
    }

    std::shared_ptr<Sink> m_sink;

    mutable std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<Message> m_queue;
    const size_t m_capacity;

    std::atomic<Level> m_level;
    std::atomic<Level> m_breakLevel{Level::Critical};
    std::atomic<bool> m_breakEnabled{false};
    std::atomic<BreakMode> m_breakMode{BreakMode::DebugBreak};
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_flushRequested{false};
    std::atomic<bool> m_timestampsEnabled{true};

    std::thread m_worker;
    BufferPool m_pool{512, 1024}; // default pool: 1024 blocks of 512 bytes
    std::mutex m_poolMtx;
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
