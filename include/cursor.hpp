#pragma once
#include <ctls/ctls.hpp>
#include <memory>
#include <utility>
#include <optional>
#include <string>

#include "types.hpp"

namespace csql {
    class sqlite_row;
    class connection;

    class cursor {
    public:
        explicit cursor(std::shared_ptr<connection> conn, sqlite3_stmt_ptr stmt)
            : _conn{std::move(conn)}, _stmt{std::move(stmt)} {
        }

        ~cursor();

        cursor(const cursor&) = delete;

        cursor& operator=(const cursor&) = delete;

        cursor(cursor&&) = default;

        cursor& operator=(cursor&&) = default;

        ctls::result<sqlite_row> next();

    private:
        std::shared_ptr<connection> _conn;
        std::optional<std::string> _error;
        sqlite3_stmt_ptr _stmt;
        bool _is_end = false;
    };

} // namespace csql