#pragma once
#include <functional>

#include <ctls/ctls.hpp>

#include "database_trx.hpp"

namespace csql {
    template <class T>
    ctls::result<T>
    manager::transact(const std::function<ctls::result<T>(database_trx&)>& fn) {
        auto db = get_database();
        if (!db) {
            return db;
        }

        std::unique_lock lock{(*db)->get_mutex()};
        database_trx trx_db{*db, lock};

        if (auto res = trx_db.begin(); !res) {
            return res;
        }

        auto res = fn(trx_db);

        if (!res) {
            if (auto rb = trx_db.rollback(lock); !rb) {
                return ctls::fails(res, rb);
            }
            return res;
        }

        if (auto commit = trx_db.commit(lock); !commit) {
            return commit;
        }

        return res;
    }
} // namespace csql
