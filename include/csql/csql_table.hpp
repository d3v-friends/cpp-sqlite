#pragma once
#include <format>
#include <string>
#include <vector>

#include "table.hpp"
#include "types.hpp"

namespace csql {
    class database;
    class csql_table : public table {
    public:
        explicit csql_table() : table{} {}
        ~csql_table() override = default;
        csql_table(const csql_table&) = delete;
        csql_table& operator=(const csql_table&) = delete;
        csql_table(csql_table&&) = delete;
        csql_table& operator=(csql_table&&) = delete;

        std::string get_table_name() override {
            return _table_name;
        }

        std::vector<std::string> get_migrations() override {
            static std::vector res{std::format(
                "CREATE TABLE IF NOT EXISTS {}("
                "table_name TEXT PRIMARY KEY,"
                "version INTEGER NOT NULL DEFAULT 0 "
                ") WITHOUT ROWID",
                get_table_name())};

            return res;
        }

        sqlite_int64 get_version(database& db, const table& table);

    private:
        std::string _table_name = "csql";
    };
} // namespace csql
