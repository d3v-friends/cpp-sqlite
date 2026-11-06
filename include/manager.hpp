#pragma once
#include <mutex>
#include <cstdint>
#include <sqlite3.h>
#include <deque>
#include <string>
#include <filesystem>
#include <condition_variable>
#include <vector>

#include "types.hpp"

namespace csql {
    class client;

    class manager {
    public:
        explicit manager() = default;
        ~manager();
        manager(const manager&) = delete;
        manager& operator=(const manager&) = delete;
        manager(manager&&) = delete;
        manager& operator=(manager&&) = delete;

        bool init(
            const std::filesystem::path& filepath,
            std::size_t max_connections = 10,
            int wait_busy_milli = 5000);

        std::unique_ptr<client> get_client();

    private:
        mutable std::mutex _mutex;
        std::deque<sqlite3_ptr> _idle_connections;
        std::vector<sqlite3_ptr> _in_progress_connections;
        std::size_t _max_connections = 0;
        std::condition_variable _is_callable;
        e_manager_status _status = e_manager_status::init;
        int _wait_busy_milli = 0;
        unsigned int _wait_connection = 0; // 커넥션을 발급받으려는 프로세스 개수

    private:
        void release(sqlite3* db);
        bool init_db(sqlite3* db) const;
    };
} // namespace sql
