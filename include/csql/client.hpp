#pragma once
#include <ctls/ctls.hpp>
#include <memory>
#include <string>
#include <sqlite3.h>
#include <utility>

namespace csql {
    class connection;
    class cursor;

    class client {
    public:
        explicit client(std::shared_ptr<connection> conn) : _conn{std::move(conn)} {}

        ~client();

        client(const client&) = delete;

        client& operator=(const client&) = delete;

        client(client&&) = default;

        client& operator=(client&&) = default;

        [[nodiscard]] ctls::status exec(const std::string& sql) const;

        template <typename... ARGS>
        std::unique_ptr<cursor> query(const std::string& sql, ARGS&&... args);

    private:
        std::shared_ptr<connection> _conn;

        template <class T> int bind_stmt(sqlite3_stmt* stmt, int i, T& value);
    };
} // namespace csql

#include "client.inl"
