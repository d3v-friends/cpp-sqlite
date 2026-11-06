#pragma once
#include <memory>
#include <optional>
#include "types.hpp"

namespace csql {
    class sqlite_row;
    class connection;

    class cursor {
    public:
        explicit cursor(std::shared_ptr<connection> conn, sqlite3_stmt_ptr stmt)
            : _conn{std::move(conn)}, _stmt{std::move(stmt)} {}
        ~cursor();
        cursor(const cursor&) = delete;
        cursor& operator=(const cursor&) = delete;
        cursor(cursor&&) = default;
        cursor& operator=(cursor&&) = default;

        std::optional<sqlite_row> next();

    private:
        std::shared_ptr<connection> _conn;
        sqlite3_stmt_ptr _stmt;
    };

} // namespace csql