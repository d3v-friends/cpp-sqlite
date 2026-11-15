#pragma once

#include <format>
#include <string>
#include <vector>

#include "table_base.hpp"
#include "types.hpp"

namespace csql {

    class csql_table : public table_base {
    public:
        explicit csql_table() = default;
        ~csql_table() override = default;
        csql_table(const csql_table&) = delete;
        csql_table& operator=(const csql_table&) = delete;
        csql_table(csql_table&&) = delete;
        csql_table& operator=(csql_table&&) = delete;

        std::string get_table_name() override {
            return _table_name;
        }

        std::vector<std::string> get_migrations() override {
            std::vector res{std::format(
                "CREATE TABLE IF NOT EXISTS {} ("
                "table_name TEXT PRIMARY KEY,"
                "version INTEGER NOT NULL DEFAULT 0 "
                ") WITHOUT ROWID",
                _table_name)};
            return res;
        }

    private:
        std::string _table_name{CSQL_TABLE_NAME};
    };
} // namespace csql
