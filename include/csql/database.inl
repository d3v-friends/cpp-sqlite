#pragma once

#include <cstddef>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "cursor.hpp"
#include "stmt.hpp"
#include "types.hpp"

namespace csql {
    template <typename... ARGS>
    ctls::result<cursor> database::query(
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
        // prepare 에 int 로 넘기므로 그 이상은 받지 않는다
        if (sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            [[unlikely]] {
            return ctls::failed("sql is too long: size={}", sql.size());
        }
        // SQLite 는 NUL 에서 sql 을 끊어 읽어서, 뒤의 문장이 조용히 버려진다
        if (sql.find('\0') != std::string::npos) [[unlikely]] {
            return ctls::failed("sql contains NUL");
        }

        // shared_from_this() 는 예외를 던지므로 weak 로 확인한다
        const auto self = weak_from_this().lock();
        if (self == nullptr) [[unlikely]] {
            return ctls::failed("database is not owned by shared_ptr");
        }

        sqlite3_stmt* raw = nullptr;
        const char* tail = nullptr;
        const char* const end = sql.data() + sql.size();
        if (sqlite3_prepare_v2(
                _db, sql.data(), static_cast<int>(sql.size()), &raw, &tail) !=
            SQLITE_OK) {
            return ctls::failed(
                "failed to prepare sql: sql={}, error={}", sql, sqlite3_errmsg(_db));
        }

        // 여기서부터는 어느 경로로 나가도 stmt 가 자동으로 finalize 된다
        sqlite3_stmt_ptr stmt{raw};
        if (stmt == nullptr) {
            return ctls::failed("sql has no statement: sql={}", sql);
        }

        // 남은 부분을 다시 prepare 해서 진짜 문장이 있는지 확인한다 (공백, 주석, ; 는
        // 허용)
        while (tail != nullptr && tail < end) {
            sqlite3_stmt* rest = nullptr;
            const char* next = nullptr;
            const auto rc =
                sqlite3_prepare_v2(_db, tail, static_cast<int>(end - tail), &rest, &next);
            sqlite3_finalize(rest);
            if (rc != SQLITE_OK || rest != nullptr) {
                return ctls::failed("multiple statements not supported: sql={}", sql);
            }
            if (next == tail) {
                break;
            }
            tail = next;
        }

        // ? 수 검사 (가장 큰 번호 기준이라 ?1 || ?1 은 1개)
        if (const int need = sqlite3_bind_parameter_count(stmt.get());
            need != static_cast<int>(sizeof...(ARGS))) [[unlikely]] {
            return ctls::failed(
                "bind count mismatch: sql={}, need={}, got={}", sql, need,
                sizeof...(ARGS));
        }

        // 실패한 인자에서 멈추므로 i 가 실패한 번호다. errmsg 는 finalize 전에 읽힌다
        if (int i = 0; !((bind_stmt(stmt.get(), ++i, args) == SQLITE_OK) && ...)) {
            return ctls::failed(
                "failed to bind: sql={}, index={}, error={}", sql, i,
                sqlite3_errmsg(_db));
        }

        return cursor{shared_from_this(), std::move(stmt)};
    }

    template <typename... ARGS>
    ctls::result<cursor> database::query(const std::string& sql, ARGS&&... args) {
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
        if (auto res = cur->next(lock); !res) {
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
