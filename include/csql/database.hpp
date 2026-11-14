#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

namespace csql {
    class cursor;
    class database_trx;

    // shared_ptr 로 감싸서 공유한다
    // 1개의 커넥션에서 1개의 쿼리씩 돌려야 하므로 여기서 mutex 잡아줘야 한다.
    class database : public std::enable_shared_from_this<database> {
    public:
        explicit database(sqlite3* db, std::function<void(sqlite3* db)> on_release)
            : _on_release{std::move(on_release)}, _db{db} {
            IASSERT(_on_release != nullptr, "on_release is empty");
        }

        ~database() {
            IASSERT(_on_release != nullptr, "on_release is empty");
            _on_release(_db);
        }

        database(const database&) = delete;

        database& operator=(const database&) = delete;

        database(database&&) = delete;

        database& operator=(database&&) = delete;

        [[nodiscard]] sqlite3* get_sqlite3() const noexcept {
            return _db;
        }

        [[nodiscard]] std::mutex& get_mutex() const noexcept {
            return _mutex;
        }

        template <typename... ARGS>
        ctls::result<std::unique_ptr<cursor>>
        query(std::unique_lock<std::mutex>& lock, const std::string& sql, ARGS&&... args);

        template <typename... ARGS>
        ctls::result<std::unique_ptr<cursor>>
        query(const std::string& sql, ARGS&&... args);

        template <typename... ARGS>
        ctls::result<>
        exec(std::unique_lock<std::mutex>& lock, const std::string& sql, ARGS&&... args);

        template <typename... ARGS>
        ctls::result<> exec(const std::string& sql, ARGS&&... args);

    private:
        mutable std::mutex _mutex;
        const std::function<void(sqlite3* db)> _on_release;
        sqlite3* _db;
    };

} // namespace csql

#include "database.inl"
