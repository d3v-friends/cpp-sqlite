#pragma once
#include <string>
#include <filesystem>
#include <condition_variable>
#include <vector>
#include <mutex>

#include "db.hpp"
#include "types.hpp"

namespace sql {
    class client;

    // manager 는 main 에서 선언하고 참조하여 쓰는 방식으로 구성한다.
    class manager {
    public:
        explicit manager(
            const std::string& db_path,
            const std::size_t max_connections = 10)
            : _db_path{db_path}, _max_connections{max_connections},
              _idle_db{max_connections} {}
        ~manager();
        manager(const manager&) = delete;
        manager& operator=(const manager&) = delete;
        manager(manager&&) = delete;
        manager& operator=(manager&&) = delete;

        [[nodiscard]] bool connect();

        [[nodiscard]] std::unique_ptr<client> get_client();

    private:
        mutable std::mutex _mutex;
        std::vector<std::unique_ptr<db>> _dbs;
        std::condition_variable _notify_full;
        const std::filesystem::path _db_path;
        const std::size_t _max_connections;
        std::size_t _idle_db;     // 쉬고 있는 커넥션 수
        std::size_t _wait_db = 0; // 커넥션 받기 원하는 수 (대기중)
        e_manager_status _status = e_manager_status::init;

    private:
        friend db;
        void release();
    };
} // namespace sql
