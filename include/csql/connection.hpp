#pragma once
#include <sqlite3.h>
#include <functional>
#include <utility>
#include <mutex>
#include <stdexcept>

namespace csql {
    // shared_ptr 로 감싸서 공유한다
    // 1개의 커넥션에서 1개의 쿼리씩 돌려야 하므로 여기서 mutex 잡아줘야 한다.
    class connection {
    public:
        explicit connection(sqlite3* db, std::function<void(sqlite3* db)> on_release)
            : _on_release{std::move(on_release)}, _db{db} {
            if (!_on_release) {
                throw std::invalid_argument("connection: on_release is empty");
            }
        }

        ~connection() {
            if (_on_release) {
                _on_release(_db);
            }
        }

        connection(const connection&) = delete;

        connection& operator=(const connection&) = delete;

        connection(connection&&) = delete;

        connection& operator=(connection&&) = delete;

        [[nodiscard]] sqlite3* get_db() const noexcept { return _db; }
        [[nodiscard]] std::mutex& get_mutex() const noexcept { return _mutex; }

    private:
        mutable std::mutex _mutex;
        const std::function<void(sqlite3* db)> _on_release;
        sqlite3* _db;
    };
} // namespace csql