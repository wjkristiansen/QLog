#include "QLog.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdarg>
#include <csignal>

namespace QLog
{

namespace
{
    inline void DebugBreakNow()
    {
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    // As a fallback, raise SIGTRAP where available
#  ifdef SIGTRAP
    std::raise(SIGTRAP);
#  else
    // no-op
#  endif
#endif
    }
}

const char* ToString(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
        case Level::Critical: return "CRITICAL";
        case Level::Off: return "OFF";
        default: return "?";
    }
}

std::string FormatTimestamp(const Message& message)
{
    if (!message.timestamp.has_value()) {
        return "";
    }

    const auto tp = *message.timestamp;
    const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(tp - secs).count();

    std::time_t t = std::chrono::system_clock::to_time_t(secs);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char dateTimeBuf[32];
    std::snprintf(dateTimeBuf, sizeof(dateTimeBuf), "[%04d-%02d-%02d %02d:%02d:%02d.%06d] ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(us));

    return std::string(dateTimeBuf);
}

OStreamSink::OStreamSink(std::ostream& os)
    : m_os(os)
{}

void OStreamSink::Write(const Message& message)
{
    m_os << FormatTimestamp(message) << ToString(message.level) << ": " << message.text << '\n';
}

void OStreamSink::Flush()
{
    m_os.flush();
}

Logger::Logger(Sink& sink, Level initialLevel, size_t capacity)
    : m_sink(&sink, [](Sink*) {}), m_capacity(capacity), m_level(initialLevel)
{
    m_worker = std::thread([this]
    {
        Worker();
    });
}

Logger::~Logger()
{
    Shutdown();
}

void Logger::SetLevel(Level level)
{
    m_level.store(level, std::memory_order_relaxed);
}

void Logger::Log(Level level, const char* format, va_list args)
{
    if (level < m_level.load(std::memory_order_relaxed))
    {
        return; // filtered out cheaply
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int needed = std::vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);
    if (needed < 0)
    {
        return;
    }
    std::string message(needed + 1, '\0');
    int len = std::vsnprintf(message.data(), message.size(), format, args);
    if (len < 0) return;
    message.resize(len);

    // Now call the std::string Log, but since we're removing it, inline the logic.

    // Optional debug break if configured
    if (m_breakEnabled.load(std::memory_order_relaxed) && level >= m_breakLevel.load(std::memory_order_relaxed))
    {
        if (m_breakMode.load(std::memory_order_relaxed) == BreakMode::Throw)
        {
            throw BreakException{};
        }
        else
        {
            DebugBreakNow();
        }
    }
    // Allocate storage for message text from pool (or heap fallback)
    const auto size = message.size();
    BufferPool::Allocation alloc;
    {
        std::lock_guard<std::mutex> lk(m_poolMtx);
        alloc = m_pool.Allocate(size);
    }
    std::memcpy(alloc.ptr, message.data(), size);

    Message msg;
    msg.level = level;
    if (m_timestampsEnabled.load(std::memory_order_relaxed))
    {
        msg.timestamp = std::chrono::system_clock::now();
    }
    else
    {
        msg.timestamp = std::nullopt;
    }
    msg.text = std::string_view(static_cast<const char*>(alloc.ptr), size);
    msg.storage = alloc.ptr;
    msg.storageSize = alloc.size;
    msg.storagePooled = alloc.pooled;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (!m_running.load(std::memory_order_relaxed))
        {
            // release allocation and bail
            ReleaseMessageStorage(msg);
            return;
        }
        if (m_capacity != 0 && m_queue.size() >= m_capacity)
        {
            // drop oldest to keep tail recent without blocking
            ReleaseMessageStorage(m_queue.front());
            m_queue.pop_front();
        }
        m_queue.push_back(std::move(msg));
    }
    m_cv.notify_one();
}

void Logger::Log(Level level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Log(level, format, args);
    va_end(args);
}

void Logger::Flush()
{
    m_flushRequested.store(true, std::memory_order_relaxed);
    m_cv.notify_one();
}

void Logger::Shutdown()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false))
    {
        return; // already stopped
    }
    m_cv.notify_one();
    if (m_worker.joinable())
        m_worker.join();
}

void Logger::Worker()
{
    std::unique_lock<std::mutex> lock(m_mtx);
    while (m_running.load(std::memory_order_relaxed) || !m_queue.empty())
    {
        m_cv.wait(lock, [&]
        {
            return !m_running.load(std::memory_order_relaxed) || !m_queue.empty() || m_flushRequested.load(std::memory_order_relaxed);
        });

        while (!m_queue.empty())
        {
            Message msg = std::move(m_queue.front());
            m_queue.pop_front();
            // Unlock while writing to sink to avoid blocking producers
            lock.unlock();
            try
            {
                m_sink->Write(msg);
            }
            catch (...)
            {
                // Swallow sink exceptions to keep worker alive
            }
            // Release any storage used by this message text
            ReleaseMessageStorage(msg);
            lock.lock();
        }

        if (m_flushRequested.exchange(false))
        {
            lock.unlock();
            try
            {
                m_sink->Flush();
            }
            catch (...)
            {
            }
            lock.lock();
        }
    }
    // Final flush on exit
    lock.unlock();
    try
    {
        m_sink->Flush();
    }
    catch (...)
    {
    }
}

} // namespace QLog
