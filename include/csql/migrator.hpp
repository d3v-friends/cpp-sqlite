#pragma once

#include <ctls/ctls.hpp>

#include "types.hpp"

namespace csql {
    class database_trx;
    class table_base;
    class migrator {
    public:
        explicit migrator(database_trx& db) : _db{db} {}
        ~migrator() = default;
        migrator(const migrator&) = delete;
        migrator& operator=(const migrator&) = delete;
        migrator(migrator&&) = delete;
        migrator& operator=(migrator&&) = delete;

        ctls::result<sqlite_int64> get_version(table_base* table) const;

        ctls::result<bool> has_table(table_base* table) const;

        ctls::result<> update_version(table_base* table, sqlite_int64 version) const;

    private:
        database_trx& _db;
    };
} // namespace csql
