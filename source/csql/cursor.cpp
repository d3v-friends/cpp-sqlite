#include <memory>
#include <ctls/ctls.hpp>
#include <mutex>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>
#include <variant>
#include <sqlite3.h>
#include <optional>
#include <string>

#include "csql/types.hpp"
#include "csql/cursor.hpp"
#include "csql/connection.hpp"
#include "csql/sqlite_row.hpp"

namespace csql {
    cursor::~cursor() = default;

    ctls::result<sqlite_row> cursor::next() {
        std::scoped_lock lock{_conn->get_mutex()};

        if (_error) [[unlikely]] {
            return ctls::failed("{}", *_error);
        }

        const auto stmt = _stmt.get();
        const auto db = _conn->get_db();

        if (_is_end) [[unlikely]] {
            return sqlite_row{};
        }

        switch (sqlite3_step(stmt)) {
        case SQLITE_DONE:
            _is_end = true;
            return sqlite_row{};
        case SQLITE_ROW:
            // 한줄 읽었음
            break;
        default:
            _error = std::format("failed step stmt: error={}", sqlite3_errmsg(db));
            return ctls::failed("{}", *_error);
        }

        const auto total_columns = sqlite3_column_count(stmt);
        std::vector<sqlite_values> values;
        values.reserve(static_cast<std::size_t>(total_columns));

        for (int i = 0; i < total_columns; ++i) {
            switch (sqlite3_column_type(stmt, i)) {
            case SQLITE_INTEGER:
                values.emplace_back(sqlite_int64{sqlite3_column_int64(stmt, i)});
                break;
            case SQLITE_FLOAT:
                values.emplace_back(sqlite_double{sqlite3_column_double(stmt, i)});
                break;
            case SQLITE_TEXT: {
                // 텍스트 부터 불러야 한다. 우선순위 중요
                const auto* p = reinterpret_cast<const char*>(
                    sqlite3_column_text(stmt, i));
                const auto n = static_cast<std::size_t>(sqlite3_column_bytes(stmt, i));
                if (p) {
                    values.emplace_back(
                        std::in_place_type<sqlite_text>, p, n);
                } else {
                    values.emplace_back(sqlite_text{});
                }
                break;
            }
            case SQLITE_BLOB: {
                const auto* p = static_cast<const std::uint8_t*>(sqlite3_column_blob(
                    stmt, i));
                const auto n = static_cast<std::size_t>(sqlite3_column_bytes(stmt, i));
                if (p) {
                    values.emplace_back(std::in_place_type<sqlite_blob>, p, p + n);
                } else {
                    values.emplace_back(sqlite_blob{});
                }
                break;
            }
            case SQLITE_NULL:
                values.emplace_back(sqlite_null{});
                break;
            default:
                // 지정된 타입이 아니면 에러를 발생하고 더 이상 추출하지 않는다.
                _error = std::format("unknown column type: index={}", i);
                return ctls::failed("{}", *_error);
            }
        }

        return sqlite_row{std::move(values)};
    }
} // namespace csql
