#include "csql/migrator.hpp"

#include <format>
#include <string>

#include <ctls/ctls.hpp>

#include "csql/database_trx.hpp"
#include "csql/table_base.hpp"
#include "csql/types.hpp"

namespace csql {
    ctls::result<sqlite_int64> migrator::get_version(table_base* table) const {
        auto has = has_table(table);
        if (!has) {
            return has;
        }

        if (!*has) {
            return sqlite_int64{0};
        }

        auto res = _db.query(
            std::format("SELECT version FROM {} WHERE table_name = ?", CSQL_TABLE_NAME),
            table->get_table_name());

        if (!res) {
            return res;
        }

        auto row = res->next();

        if (!row) {
            return row;
        }

        if (row->is_empty()) {
            return sqlite_int64{0}; // 행 없음 = 버전 0
        }

        return row->get<sqlite_int64>("version");
    }

    ctls::result<bool> migrator::has_table(table_base* table) const {
        auto res = _db.query(
            "SELECT count(*) AS n FROM sqlite_master "
            "WHERE type = 'table' AND name = ? COLLATE NOCASE;",
            table->get_table_name());

        if (!res) {
            return res;
        }

        auto cur = res->next();
        if (!cur) {
            return cur;
        }

        auto n = cur->get<sqlite_int64>("n");

        if (!n) {
            return n;
        }

        return 0 < *n;
    }

    ctls::result<>
    migrator::update_version(table_base* table, sqlite_int64 version) const {
        const auto sql = std::format(
            "INSERT INTO {}(table_name, version) VALUES(?, ?) "
            "ON CONFLICT(table_name) DO UPDATE SET version = excluded.version",
            CSQL_TABLE_NAME);
        return _db.exec(sql, table->get_table_name(), version);
    }
} // namespace csql
