#pragma once

#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "cursor.hpp"
#include "sqlite_row.hpp"
#include "stmt.hpp"
#include "types.hpp"

namespace csql {
    template <typename... ARGS>
    ctls::result<std::unique_ptr<cursor>> database::query(
        std::unique_lock<std::mutex>& lock, const std::string& sql, ARGS&&... args) {
        if (lock.mutex() != &_mutex) [[unlikely]] {
            return ctls::failed("mutex is not same");
        }

        if (!lock.owns_lock()) [[unlikely]] {
            lock.lock();
        }

        if (sql.empty()) [[unlikely]] {
            return ctls::failed("sql is empty");
        }

        sqlite3_stmt* stmt = nullptr;
        const char* tail = nullptr;

        const auto res = sqlite3_prepare_v2(
            _db, sql.data(), static_cast<int>(sql.size()), &stmt, &tail);
        if (res != SQLITE_OK) {
            return ctls::failed(
                "failed to prepare sql: sql={}, error={}", sql, sqlite3_errmsg(_db));
        }

        if (stmt == nullptr) {
            return ctls::failed("sql has no statement: sql={}", sql);
        }

        if (tail && std::string_view{tail}.find_first_not_of(" \t\r\n;") !=
                        std::string_view::npos) {
            sqlite3_finalize(stmt);
            return ctls::failed("multiple statements not supported: sql={}", sql);
        }

        // ? 수 검사
        if (const int need = sqlite3_bind_parameter_count(stmt);
            need != static_cast<int>(sizeof...(ARGS))) [[unlikely]] {
            sqlite3_finalize(stmt);
            return ctls::failed(
                "bind count mismatch: sql={}, need={}, got={}", sql, need,
                sizeof...(ARGS));
        }

        if (int i = 0; !((bind_stmt(stmt, ++i, args) == SQLITE_OK) && ...)) {
            const auto message = std::format(
                "failed to bind: sql={}, index={}, error={}", sql, i,
                sqlite3_errmsg(_db));
            sqlite3_finalize(stmt);
            return ctls::failed(message);
        }

        return std::make_unique<cursor>(shared_from_this(), sqlite3_stmt_ptr{stmt});
    }

    template <typename... ARGS>
    ctls::result<std::unique_ptr<cursor>>
    database::query(const std::string& sql, ARGS&&... args) {
        std::unique_lock lock{_mutex};
        return query(lock, sql, std::forward<ARGS>(args)...);
    }

    template <typename... ARGS>
    ctls::result<> database::exec(
        std::unique_lock<std::mutex>& lock, const std::string& sql, ARGS&&... args) {
        if (lock.mutex() != &_mutex) [[unlikely]] {
            return ctls::failed("mutex is not same");
        }

        if (!lock.owns_lock()) [[unlikely]] {
            lock.lock();
        }

        auto cur = query(lock, sql, std::forward<ARGS>(args)...);

        if (!cur) {
            return cur;
        }

        // 쿼리는 1개만 허용되므로 1번만 실행한다.
        if (auto res = (*cur)->next(lock); !res) {
            return res;
        }

        return ctls::ok;
    }

    template <typename... ARGS>
    ctls::result<> database::exec(const std::string& sql, ARGS&&... args) {
        std::unique_lock lock{_mutex};
        return exec(lock, sql, std::forward<ARGS>(args)...);
    }
} // namespace csql
