#include <chrono>
#include <iostream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <set>
#include <vector>
#include <format>
#include "logger.h"

namespace fs = std::filesystem;


class DirWatcherCallbackBase
{
public:
    DirWatcherCallbackBase(void) = default;
    virtual ~DirWatcherCallbackBase(void) = default;

    enum class action_t { CREATE, MODIFY, DELETE, UNEXPECTED_ACTION };
    enum class file_t { DIRECTORY, REGULAR, UNEXPECTED_FILE };

    static char const* get_action_str(action_t action)
    {
        switch (action)
        {
        case DirWatcherCallbackBase::action_t::CREATE:
            return "created";
        case DirWatcherCallbackBase::action_t::MODIFY:
            return "modified";
        case DirWatcherCallbackBase::action_t::DELETE:
            return "deleted";
        case DirWatcherCallbackBase::action_t::UNEXPECTED_ACTION:
            throw std::runtime_error("Unexpected action has been detected");
        default:
            return nullptr;
        }
    }

    static char const* get_file_str(const file_t file)
    {
        switch (file)
        {
        case DirWatcherCallbackBase::file_t::DIRECTORY:
            return "Directory";
        case DirWatcherCallbackBase::file_t::REGULAR:
            return "Regular file";
        case DirWatcherCallbackBase::file_t::UNEXPECTED_FILE:
            throw std::runtime_error("Action has been detected for unexpected file type");
        default:
            return nullptr;
        }
    }

    virtual void log(action_t action, file_t file, std::string const& name) const = 0;
    virtual void report_action(action_t action, file_t file, fs::path const& path, std::string const& directory_path) = 0;
};

void DirWatcherCallbackBase::log(const action_t action, const file_t file, std::string const& name) const
{
    std::cout << "The " << get_file_str(file) << " " << name << " has been " << get_action_str(action) << std::endl;
}


class DirWatcher final
{
    std::set<fs::directory_entry> source_content;
    std::set<fs::directory_entry> replica_content;
    std::vector<fs::directory_entry> garbage;

    static constexpr size_t name_len = 1024;
    static inline DirWatcher* this_ptr = nullptr;

    std::atomic<bool> stop_flag;
    std::unique_ptr<std::thread> runner;

    std::string source = "Source";
    std::string replica = "Replica";
    std::string logfile;
    size_t synch_interval;

public:

    DirWatcher(std::string source_, std::string replica_, size_t synch_interval_, std::string logfile_path_)
        : stop_flag(false)
        , source(std::move(source_))
        , replica(std::move(replica_))
        , logfile(std::move(logfile_path_))
        , synch_interval(synch_interval_)
    {
        if (this_ptr)
            throw std::runtime_error("Only one instance of DirWatcher can be created");
        this_ptr = this;
    }

    ~DirWatcher(void)
    {
        stop_flag = true;
        runner->join();
        this_ptr = nullptr;
    }

    DirWatcher(const DirWatcher&) = delete;

    DirWatcher& operator=(const DirWatcher&) = delete;

    DirWatcher(DirWatcher&&) = delete;

    DirWatcher& operator=(DirWatcher&&) = delete;

    void run(DirWatcherCallbackBase* callback)
    {
        runner.reset(new std::thread(&DirWatcher::run_internal, this, callback));
    }

    void join() const
    {
        runner->join();
    }

    static DirWatcher* get_instance(void)
    {
        return DirWatcher::this_ptr;
    }

    static void stop()
    {
        if (this_ptr)
            this_ptr->~DirWatcher();
    }

private:

    void run_internal(DirWatcherCallbackBase* callback)
    {
        while (false == stop_flag.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::seconds(synch_interval));
            for (auto& entry : fs::recursive_directory_iterator(source))
            {
                auto const file_entry = source_content.find(entry);
                if (file_entry == source_content.end())
                {
                    source_content.emplace(entry);
                    replica_content.emplace(entry);
                    if (entry.is_regular_file())
                    {
                        callback->report_action(DirWatcherCallbackBase::action_t::CREATE, DirWatcherCallbackBase::file_t::REGULAR,
                            entry.path(), replica);
                    }
                    else if (entry.is_directory())
                    {
                        callback->report_action(DirWatcherCallbackBase::action_t::CREATE, DirWatcherCallbackBase::file_t::DIRECTORY,
                            entry.path(), replica);
                    }
                }
                else if (entry.last_write_time() > file_entry->last_write_time())
                {
                    if (entry.is_regular_file())
                    {
                        callback->report_action(DirWatcherCallbackBase::action_t::MODIFY, DirWatcherCallbackBase::file_t::REGULAR,
                            entry.path(), replica);
                    }
                    else if (entry.is_directory())
                    {
                        callback->report_action(DirWatcherCallbackBase::action_t::MODIFY, DirWatcherCallbackBase::file_t::DIRECTORY,
                            entry.path(), replica);
                    }
                }
            }

            for (auto& entry : source_content)
            {
                if (replica_content.contains(entry) && false == fs::exists(entry))
                {
                    if (entry.is_regular_file())
                    {
                        callback->report_action(DirWatcherCallbackBase::action_t::DELETE, DirWatcherCallbackBase::file_t::REGULAR,
                            entry.path(), replica);
                    }
                    else if (entry.is_directory())
                    {
                        callback->report_action(DirWatcherCallbackBase::action_t::DELETE, DirWatcherCallbackBase::file_t::DIRECTORY,
                            entry.path(), replica);
                    }
                    garbage.push_back(entry);
                }
            }
            for (auto& elem : garbage)
            {
                source_content.erase(elem);
            }
            garbage.clear();
        }
    }
};

class DirWatcherCallback final : public DirWatcherCallbackBase
{
    static std::string get_filename(const std::string& str)
    {
        const int elem = str.rfind('/');
        return str.substr(elem + 1, str.size());
    }

    virtual void report_action(const action_t action, const file_t file, fs::path const& path, const std::string& directory_path) override
    {
        if ((action == DirWatcherCallback::action_t::CREATE
            || action == DirWatcherCallback::action_t::MODIFY)
            && file == DirWatcherCallback::file_t::REGULAR)
        {
            Logger::logf(Logger::severity_t::INFO, __FILE__, __LINE__, "%s %s has been created in Replica%s", get_file_str(file), get_filename(path.generic_string()).c_str(), (" | " + path.generic_string()).c_str());
            fs::copy_file(path, directory_path + "/" + get_filename(path.generic_string()), fs::copy_options::overwrite_existing);
        }
        else if (action == DirWatcherCallback::action_t::DELETE && file == DirWatcherCallback::file_t::REGULAR)
        {
            Logger::logf(Logger::severity_t::INFO, __FILE__, __LINE__, "%s %s has been deleted from Replica%s", get_file_str(file), get_filename(path.generic_string()).c_str(), (" | " + path.generic_string()).c_str());
            fs::remove(directory_path + "/" + get_filename(path.generic_string()));
        }
        else if ((action == DirWatcherCallback::action_t::CREATE
            || action == DirWatcherCallback::action_t::MODIFY)
            && file == DirWatcherCallback::file_t::DIRECTORY)
        {
            fs::copy(path, directory_path + "/" + get_filename(path.generic_string()), fs::copy_options::overwrite_existing |
                fs::copy_options::recursive);
            Logger::logf(Logger::severity_t::INFO, __FILE__, __LINE__, "%s %s has been created in Replica%s", get_file_str(file), get_filename(path.generic_string()).c_str(), (" | " + path.generic_string()).c_str());
        }
        else if ((action == DirWatcherCallback::action_t::DELETE && file == DirWatcherCallback::file_t::DIRECTORY))
        {
            fs::remove_all(directory_path + "/" + get_filename(path.generic_string()));
            Logger::logf(Logger::severity_t::INFO, __FILE__, __LINE__, "%s %s has been deleted from Replica%s", get_file_str(file), get_filename(path.generic_string()).c_str(), (" | " + path.generic_string()).c_str());
        }
    }

    virtual void log(const action_t action, const file_t file, std::string const& name) const override
    {
        if (DirWatcherCallbackBase::action_t::UNEXPECTED_ACTION == action && DirWatcherCallbackBase::file_t::UNEXPECTED_FILE == file)
            Logger::logf(Logger::severity_t::WARNING, __FILE__, __LINE__, "Unexpected action has been detected for unexpected file type: %s", name.c_str());
        else if (DirWatcherCallbackBase::action_t::UNEXPECTED_ACTION == action)
            Logger::logf(Logger::severity_t::WARNING, __FILE__, __LINE__, "Unexpected action has been detected for %s %s", get_file_str(file), name.c_str());
        else if (DirWatcherCallbackBase::file_t::UNEXPECTED_FILE == file)
            Logger::logf(Logger::severity_t::WARNING, __FILE__, __LINE__, "Unexpected file %s has been %s", name.c_str(), get_action_str(action));
        else
            Logger::logf(Logger::severity_t::INFO, __FILE__, __LINE__, "%s %s has been %s", get_file_str(file), name.c_str(), get_action_str(action));
    }
};



void sig_handler(int sig)
{
    DirWatcher::stop();
    exit(EXIT_SUCCESS);
}

int main(const int argc, char* argv[])
{
    Logger logger(argv[4], false, false, false);

    if (5 != argc)
    {
        std::cout << "Few arguments | 1. Source folder path | 2. Replica folder path | 3. Synchronization interval | 4. Log file path and log filename" << std::endl;
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, sig_handler);

    DirWatcher watcher(argv[1], argv[2], std::atoi(argv[3]), argv[4]);
    DirWatcherCallback cb;
    watcher.run(&cb);
    watcher.join();
    return 0;
}