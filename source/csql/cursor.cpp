#include "csql/cursor.hpp"

#include <memory>
#include <mutex>

#include <ctls/ctls.hpp>

#include "csql/database.hpp"
#include "csql/query.hpp"
#include "csql/sqlite_row.hpp"

namespace csql {
    cursor::~cursor() = default;

    ctls::result<sqlite_row> cursor::next() {
        if (!_could_lock) {
            return on_next();
        }

        std::scoped_lock lock{_conn->get_mutex()};
        return on_next();
    }

    ctls::result<sqlite_row> cursor::on_next() {
        if (_is_end) {
            // 종료되었다면 빈값을 내보낸다.
            return sqlite_row{};
        }

        auto res = step_stmt(_conn->get_sqlite3(), _stmt.get());
        if (!res || res->is_empty()) {
            _is_end = true;
        }
        return res;
    }
} // namespace csql
