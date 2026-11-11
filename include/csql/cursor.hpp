#pragma once
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <ctls/ctls.hpp>

#include "types.hpp"

namespace csql {
    class sqlite_row;
    class database;

    class cursor {
    public:
        explicit cursor(
            std::shared_ptr<database> conn,
            sqlite3_stmt_ptr stmt,
            const bool could_lock = true)
            : _conn{std::move(conn)}, _stmt{std::move(stmt)}, _could_lock{could_lock} {}

        ~cursor();

        cursor(const cursor&) = delete;

        cursor& operator=(const cursor&) = delete;

        cursor(cursor&&) = delete;

        cursor& operator=(cursor&&) = delete;

        ctls::result<sqlite_row> next();

        [[nodiscard]] bool is_end() const noexcept {
            return _is_end;
        }

    private:
        std::shared_ptr<database> _conn;
        std::optional<std::string> _error;
        sqlite3_stmt_ptr _stmt;
        bool _is_end = false;
        bool _could_lock;

        ctls::result<sqlite_row> on_next();
    };

} // namespace csql
