#pragma once

#include <thread>
#include <iomanip>
#include <ctime>
#include <iostream>
#include <syncstream>
#include <chrono>
#include <fstream>
#include <mutex> 


class Logger
{
public:

    enum class severity_t { INFO, WARNING, ERROR, FATAL, DEBUG };

private:

    static inline Logger* this_ptr = nullptr;
    std::ofstream* outf;
    std::ostream& out;
    bool const debug;
    bool const show_source;
    bool const colored;


public:
    Logger(char const* filename, bool debug_param = true, bool show_source_param = true, bool colored_param = true)
        : outf(filename ? new std::ofstream(filename) : nullptr)
        , out(std::cout)
        , debug(debug_param)
        , show_source(show_source_param)
        , colored(colored_param)
    {
        if (nullptr != this_ptr)
            throw std::runtime_error("Only one instance of Logger can be created");
        this_ptr = this;
        outf->open(filename);
    }

    ~Logger()
    {
        out << std::flush;
        outf->close();
        delete outf;
        this_ptr = nullptr;
    }

    Logger(const Logger&) = delete;

    Logger& operator=(const Logger&) = delete;

    Logger(Logger&&) = delete;

    Logger& operator=(Logger&&) = delete;

    template<typename ...T>
    static void logf(Logger::severity_t severity, const char* FILE, size_t LINE, char const* fmt, T&& ... args)  //__attribute__ ((format(printf, 4, 5)));
    {
        if (nullptr == this_ptr)
            throw std::runtime_error("The logger must first be instantiated");

        if (Logger::severity_t::DEBUG == severity && false == this_ptr->debug)
            return;

        this_ptr->logf_internal(severity, FILE, LINE, fmt, std::forward<T>(args)...);
    }



private:
    static char const* get_severity_str(Logger::severity_t severity)
    {
        switch (severity)
        {
        case severity_t::INFO:
            return "INFO";
            break;
        case severity_t::WARNING:
            return "WARNING";
            break;
        case severity_t::ERROR:
            return "ERROR";
            break;
        case severity_t::FATAL:
            return "FATAL";
            break;
        case severity_t::DEBUG:
            return "DEBUG";
        }
        return nullptr;
    }

    static char const* get_severity_color_str(const Logger::severity_t severity)
    {
        switch (severity)
        {
        case Logger::severity_t::ERROR:
            return "\033[31m";
        case Logger::severity_t::WARNING:
            return "\033[32m";
        case Logger::severity_t::DEBUG:
            return "\033[34m";
        default:
            return "\033[39m";
        }
    }

    template<typename ...T>
    void logf_internal(Logger::severity_t severity, const char* FILE, size_t LINE, char const* fmt, T&& ... args)
    {
        std::time_t cur_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm* cur_time_local = std::localtime(&cur_time);
        if (true == show_source)
        {
            std::osyncstream(out) << get_severity_color_str(severity) << std::put_time(cur_time_local, "%Y/%m/%d %H:%M:%S") << " | " << get_severity_str(severity) << ": " << this->format(fmt, std::forward<T>(args)...) << " (FROM: " << FILE << ":" << LINE << ")" << get_severity_color_str(Logger::severity_t::INFO) << std::endl;
            std::osyncstream(*outf) << std::put_time(cur_time_local, "%Y/%m/%d %H:%M:%S") << " | " << get_severity_str(severity) << ": " << this->format(fmt, std::forward<T>(args)...) << " (FROM: " << FILE << ":" << LINE << ")" << std::endl;
        }
        else
        {
            std::osyncstream(out) << get_severity_color_str(severity) << std::put_time(cur_time_local, "%Y/%m/%d %H:%M:%S") << " | " << get_severity_str(severity) << ": " << this->format(fmt, std::forward<T>(args)...) << get_severity_color_str(Logger::severity_t::INFO) << std::endl;
            std::osyncstream(*outf) << std::put_time(cur_time_local, "%Y/%m/%d %H:%M:%S") << " | " << get_severity_str(severity) << ": " << this->format(fmt, std::forward<T>(args)...) << " (FROM: " << FILE << ":" << LINE << ")" << std::endl;
        }
    }

    template<typename ... Args>
    std::string format(char const* format, Args&& ... args)
    {
        const int size_s = std::snprintf(nullptr, 0, format, args ...) + 1;
        if (size_s <= 0) { throw std::runtime_error("Error during formatting."); }
        const auto size = static_cast<size_t>(size_s);
        const auto buf = std::make_unique<char[]>(size);
        std::snprintf(buf.get(), size, format, args ...);
        return std::string(buf.get(), buf.get() + size - 1);
    }
};