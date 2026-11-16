#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <ctls/ctls.hpp>

#include "sqlite_row.hpp"
#include "types.hpp"
namespace csql {

    class database;

    class cursor {
    public:
        explicit cursor(std::shared_ptr<database> conn, sqlite3_stmt_ptr stmt)
            : _conn{std::move(conn)}, _stmt{std::move(stmt)} {}

        ~cursor();
        cursor(const cursor&) = delete;
        cursor& operator=(const cursor&) = delete;
        cursor(cursor&&) = default;
        cursor& operator=(cursor&&) = delete;

        ctls::result<sqlite_row> next(std::unique_lock<std::mutex>& lock);

        ctls::result<sqlite_row> next();

        [[nodiscard]] bool is_end() const noexcept {
            return _is_end;
        }

    private:
        std::shared_ptr<database> _conn;
        std::optional<std::string> _error;
        sqlite3_stmt_ptr _stmt;
        bool _is_end = false;

        ctls::result<sqlite_row> step_stmt(sqlite3* db, sqlite3_stmt* stmt);
    };

} // namespace csql
