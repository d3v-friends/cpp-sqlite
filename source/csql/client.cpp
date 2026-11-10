#include <ctls/ctls.hpp>
#include <sqlite3.h>
#include <string>
#include <format>

#include "csql/client.hpp"
#include "csql/connection.hpp"

namespace csql {
    client::~client() = default;

    ctls::status client::exec(const std::string& sql) const {
        if (sql.empty()) {
            return ctls::failed("sql must not be empty");
        }
        char* err = nullptr;
        if (sqlite3_exec(_conn->get_db(), sql.c_str(), nullptr, nullptr, &err) !=
            SQLITE_OK) {
            const auto message = std::format(
                "sqlite3_exec failed: error={}", err ? err : "unknown error");
            sqlite3_free(err);
            return ctls::failed(message);
        }

        return ctls::ok();
    }

} // namespace sql
