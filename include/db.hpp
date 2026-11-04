#pragma once
#include <utility>
#include <mutex>
#include <string>
#include <memory>

#include "types.hpp"

namespace sql {
    class manager;
    class client;

    class db {
    public:
        explicit db(sqlite3_ptr db, manager* manager, const int wait_busy_milli = 5000)
            : _db{std::move(db)}, _manager{manager}, _wait_busy_milli{wait_busy_milli} {}
        ~db();
        db(const db&) = delete;
        db& operator=(const db&) = delete;
        db(db&&) = delete;
        db& operator=(db&&) = delete;

        // 쿼리를 실행한다
        [[nodiscard]] bool exec(const std::string& sql) const;

        // 실패시 nullptr 반환, 콜함수에서 nullptr 체크 해준다
        template <typename... ARGS>
        [[nodiscard]] sqlite3_stmt_ptr
        query(const std::string& sql, ARGS&&... args);

    private:
        mutable std::mutex _mutex;
        sqlite3_ptr _db = nullptr;
        manager* _manager;
        int _wait_busy_milli;
        e_db_status _status = e_db_status::init;
        e_db_pool_status _pool_status = e_db_pool_status::idle;

        bool exec_unlock(const std::string& sql) const;

        template <class T> int bind_stmt(sqlite3_stmt* stmt, int i, T& value);

    private:
        friend manager;

        // 아이들 상태라면 true 반납하고 사용중으로 변경, 사용중이라면 false 반납
        bool can_acquire();

        // 초기화 할때 한번 실행
        bool init();

    private:
        friend client;
        // 반납 상태로 변경하기
        void release();
    };
} // namespace sql

#include "db.inl"