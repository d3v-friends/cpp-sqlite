#include "csql/csql_table.hpp"

#include <string>
#include <vector>

#include "csql/database_trx.hpp"

namespace csql {
    std::vector<std::string> csql_table::get_migrations() {
        std::vector res{std::format(
            "CREATE TABLE IF NOT EXISTS {}("
            "table_name TEXT PRIMARY KEY,"
            "version INTEGER NOT NULL DEFAULT 0 "
            ") WITHOUT ROWID",
            _table_name)};
        return res;
    }

    sqlite_int64 csql_table::get_version(table_base* table) {
        auto res = _db.query(
            std::format("SELECT version FROM {} WHERE table_name = ?", _table_name),
            table->get_table_name());

        if (!res) {
            return 0;
        }

        const auto cur = res->next();
        if (!cur) {
            return 0;
        }

        auto version = cur->get<sqlite_int64>("version");
        if (!version) {
            return 0;
        }

        return *version;
    }

    bool csql_table::has_table(table_base* table) const {
        auto res = _db.query(
            "SELECT count(*) AS n FROM sqlite_master"
            "WHERE type = 'table' AND name = ? COLLATE NOCASE;",
            table->get_table_name());

        if (!res) {
            return false;
        }

        auto cur = res->next();
        if (!cur) {
            return false;
        }

        auto n = cur->get<sqlite_int64>("n");

        if (!n) {
            return false;
        }

        return 0 < *n;
    }
} // namespace csql
