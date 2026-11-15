#pragma once
#include <format>
#include <string>
#include <vector>

#include "table_base.hpp"
#include "types.hpp"

namespace csql {
    class database_trx;
    class csql_table : public table_base {
    public:
        explicit csql_table(database_trx& db) : table_base{}, _db{db} {}
        ~csql_table() override = default;
        csql_table(const csql_table&) = delete;
        csql_table& operator=(const csql_table&) = delete;
        csql_table(csql_table&&) = delete;
        csql_table& operator=(csql_table&&) = delete;

        std::string get_table_name() override {
            return _table_name;
        }

        std::vector<std::string> get_migrations() override;

        sqlite_int64 get_version(table_base* table);

        bool has_table(table_base* table) const;

    private:
        database_trx& _db;
        std::string _table_name = "csql";
    };
} // namespace csql
