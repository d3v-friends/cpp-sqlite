#include <ctls/ctls.hpp>

#include "client.hpp"

#include "connection.hpp"

namespace csql {
    client::~client() = default;

    bool client::exec(const std::string& sql) const {
        if (sql.empty()) {
            ILOG_ERROR("sql must not be empty");
            return false;
        }
        char* err = nullptr;
        if (sqlite3_exec(_conn->get_db(), sql.c_str(), nullptr, nullptr, &err) !=
            SQLITE_OK) {
            ILOG_ERROR("sqlite3_exec failed: {}", err);
            sqlite3_free(err);
            return false;
        }
        return true;
    }

} // namespace sql
