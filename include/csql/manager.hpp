#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "types.hpp"

namespace csql {
    class database;
    class table_base;
    class database_trx;

    class manager {
    public:
        explicit manager() = default;
        ~manager();
        manager(const manager&) = delete;
        manager& operator=(const manager&) = delete;
        manager(manager&&) = delete;
        manager& operator=(manager&&) = delete;

        ctls::result<> initialize(
            const std::filesystem::path& filepath,
            std::size_t max_connections = 10,
            int wait_busy_milli = 5000);

        ctls::result<std::shared_ptr<database>> get_database();

        // table 추상클래스라서 벨류로 못씀, unique_ptr 등으로 바꿔야 한다.
        ctls::result<> migrate(const std::span<table_base* const>& tables);

        ctls::result<> migrate(table_base* table);

        ctls::result<> transact(const std::function<ctls::result<>(database_trx&)>& fn);

        // 결과값 타입을 만드시 적어주도록 하자.
        // 람다와 리턴값 모두
        template <class T>
        ctls::result<T> transact(const std::function<ctls::result<T>(database_trx&)>& fn);

    private:
        mutable std::mutex _mutex;
        std::deque<sqlite3_ptr> _idle_connections;
        std::vector<sqlite3_ptr> _in_progress_connections;
        std::size_t _max_connections = 0;
        std::condition_variable _is_callable;
        e_manager_status _status = e_manager_status::init;
        int _wait_busy_milli = 0;
        unsigned int _wait_connection = 0; // 커넥션을 발급받으려는 프로세스 개수

        void release(sqlite3* db);
        ctls::result<> init_db(sqlite3* db) const;
    };

} // namespace csql

#include "manager.inl"
