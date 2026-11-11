#pragma once
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <sqlite3.h>

namespace csql {
    // shared_ptr 로 감싸서 공유한다
    // 1개의 커넥션에서 1개의 쿼리씩 돌려야 하므로 여기서 mutex 잡아줘야 한다.
    class database {
    public:
        explicit database(sqlite3* db, std::function<void(sqlite3* db)> on_release)
            : _on_release{std::move(on_release)}, _db{db} {
            if (!_on_release) {
                throw std::invalid_argument("connection: on_release is empty");
            }
        }

        ~database() {
            if (_on_release) {
                _on_release(_db);
            }
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

    private:
        mutable std::mutex _mutex;
        const std::function<void(sqlite3* db)> _on_release;
        sqlite3* _db;
    };
} // namespace csql
