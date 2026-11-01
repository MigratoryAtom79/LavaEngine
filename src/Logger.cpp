#include "../include/LavaEngine/Logger.hpp"

#include <iomanip>
#include <iostream>

namespace LavaEngine
{
    namespace
    {
        Logger s_instance;
    }

    Logger::Logger()
        : m_console(&std::cout)
    {
        setOutput(std::cout);
    }

    Logger::Logger(const std::string& filename)
        : m_console(&std::cout),
          m_file(filename, std::ios::app)
    {
        m_fileEnabled = m_file.is_open();
    }

    Logger::~Logger()
    {
        if (m_file.is_open())
            m_file.close();
    }

    void Logger::setLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_level = level;
    }

    LogLevel Logger::level() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        return m_level;
    }

    void Logger::setConsoleEnabled(bool enabled)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_consoleEnabled = enabled;
    }

    void Logger::setFileEnabled(bool enabled)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_fileEnabled = enabled;
    }

    void Logger::setOutput(std::ostream& stream)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_console = &stream;
    }

    Logger& Logger::instance()
    {
        return s_instance;
    }

    void Logger::log(LogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (level < m_level)
            return;

        const std::string line =
            "[" + timestamp() + "] [" +
            std::string(toString(level)) + "] " +
            message;

        if (m_consoleEnabled && m_console)
        {
            const std::string_view color = toColor(level);

            if (!color.empty())
                *m_console << color;
            *m_console << line;
            if (!color.empty())
                *m_console << "\033[0m";
            *m_console << '\n';

            m_console->flush();
        }

        if (m_fileEnabled && m_file.is_open())
        {
            m_file << line << '\n';
            m_file.flush();
        }

        m_history.push_back(
            LogEntry{
                .level = level,
                .timestamp = timestamp(),
                .message = message
            }
        );

        while (m_history.size() > m_historySize)
            m_history.pop_front();
    }

    void Logger::setHistorySize(std::size_t size)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_historySize = size;

        while (m_history.size() > m_historySize)
            m_history.pop_front();
    }

    void Logger::clearHistory()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_history.clear();
    }

    std::vector<LogEntry> Logger::entries() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        return {
            m_history.begin(),
            m_history.end()
        };
    }

    std::string_view Logger::toString(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace:   return "Trace";
            case LogLevel::Info:    return "Info";
            case LogLevel::Warning: return "Warning";
            case LogLevel::Error:   return "Error";
            case LogLevel::Fatal:   return "Fatal";
            case LogLevel::Debug:   return "Debug";
        }

        return "Unknown";
    }

    std::string_view Logger::toColor(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace:   return "\033[90m";
            case LogLevel::Info:    return "\033[34m";
            case LogLevel::Warning: return "\033[33m";
            case LogLevel::Error:   return "\033[31m";
            case LogLevel::Fatal:   return "\033[1;31m";
            case LogLevel::Debug:   return "\033[32m";
        }

        return "";
    }

    std::string Logger::timestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);

        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif

        std::ostringstream stream;
        stream << std::put_time(&local, "%H:%M:%S");
        return stream.str();
    }
}