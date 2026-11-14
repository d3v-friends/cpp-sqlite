#include "csql/database_trx.hpp"

#include <memory>
#include <mutex>
#include <string>

#include <ctls/ctls.hpp>

#include "csql/database.hpp"

namespace csql {

    ctls::result<> database_trx::rollback(std::unique_lock<std::mutex>& lock) {
        if (sqlite3_get_autocommit(_db->get_sqlite3()) != 0) {
            return ctls::ok;
        }
        return exec("ROLLBACK");
    }

    ctls::result<> database_trx::commit(std::unique_lock<std::mutex>& lock) {
        if (auto commit = exec("COMMIT"); !commit) {
            return ctls::fails(commit, rollback(lock));
        }
        return ctls::ok;
    }
} // namespace csql
