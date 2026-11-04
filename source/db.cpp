#include <mutex>
#include <sqlite3.h>
#include <string>
#include <ctls/ctls.hpp>

#include "db.hpp"
#include "manager.hpp"

namespace sql {
    db::~db() = default;
    bool db::exec(const std::string& sql) const {
        std::scoped_lock lock{_mutex};
        if (_status != e_db_status::ready) [[unlikely]] {
            return false;
        }
        return exec_unlock(sql);
    }

    bool db::can_acquire() {
        std::scoped_lock lock{_mutex};
        if (_pool_status != e_db_pool_status::idle) [[unlikely]] {
            return false;
        }
        _pool_status = e_db_pool_status::busy;
        return true;
    }

    bool db::init() {
        std::unique_lock lock{_mutex};
        if (_status != e_db_status::init) [[unlikely]] {
            ILOG_ERROR("db already initialized");
            return false;
        }
        lock.unlock();

        // 타임아웃 지정하기
        sqlite3_busy_timeout(_db.get(), _wait_busy_milli);
        if (!exec_unlock(
                "PRAGMA journal_mode=WAL;"
                "PRAGMA foreign_keys=ON;"
                "PRAGMA synchronous=NORMAL;")) [[unlikely]] {
            lock.lock();
            _status = e_db_status::terminated;
            return false;
        }

        lock.lock();
        _status = e_db_status::ready;
        return true;
    }

    bool db::exec_unlock(const std::string& sql) const {
        ILOG_TRACE("exec: sql={}", sql);
        char* err = nullptr;
        if (sqlite3_exec(_db.get(), sql.data(), nullptr, nullptr, &err) != SQLITE_OK)
            [[unlikely]] {
            std::string msg = err ? err : "unknown error";
            ILOG_ERROR("failed to execute sql: sql={}, error={}", sql, msg);
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    void db::release() {
        std::unique_lock lock{_mutex};
        _pool_status = e_db_pool_status::idle;
        lock.unlock();
        _manager->release();
    }
} // namespace sql
