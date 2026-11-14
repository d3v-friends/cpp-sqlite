#include "csql/cursor.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "csql/database.hpp"
#include "csql/sqlite_row.hpp"
#include "csql/types.hpp"

namespace csql {
    cursor::~cursor() = default;

    ctls::result<sqlite_row> cursor::next(std::unique_lock<std::mutex>& lock) {
        if (lock.mutex() != &_conn->get_mutex()) [[unlikely]] {
            return ctls::failed("mutex is not same");
        }

        if (!lock.owns_lock()) [[unlikely]] {
            lock.lock();
        }

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

    ctls::result<sqlite_row> cursor::next() {
        std::unique_lock lock{_conn->get_mutex()};
        return next(lock);
    }

    ctls::result<sqlite_row> cursor::step_stmt(sqlite3* db, sqlite3_stmt* stmt) {
        switch (sqlite3_step(stmt)) {
        case SQLITE_DONE:
            return sqlite_row{};
        case SQLITE_ROW:
            break;
        default:
            return ctls::failed("failed step stmt: error={}", sqlite3_errmsg(db));
        }

        const auto total_columns = sqlite3_column_count(stmt);
        std::map<std::string, sqlite_values> map;

        for (int i = 0; i < total_columns; ++i) {
            // key 가 메모리가 부족하면 null 나올수 있음.
            // std::string 생성자에 c배열을 입력하면 deep copy 함
            const auto* cstr = sqlite3_column_name(stmt, i);
            std::string key{cstr == nullptr ? "" : cstr};

            if (map.contains(key)) {
                return ctls::failed("duplicate column name: {}", key);
            }

            switch (sqlite3_column_type(stmt, i)) {
            case SQLITE_INTEGER:
                map.try_emplace(key, sqlite_int64{sqlite3_column_int64(stmt, i)});
                break;
            case SQLITE_FLOAT:
                map.try_emplace(key, sqlite_double{sqlite3_column_double(stmt, i)});
                break;
            case SQLITE_TEXT: {
                // BLOB 전에 텍스트 부터 불러야 한다. 우선순위 중요
                const auto* p =
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                const auto n = static_cast<std::size_t>(sqlite3_column_bytes(stmt, i));
                if (p) {
                    // try_emplace: key 뒤의 인자를 value(variant) 생성자로 그대로 전달.
                    // key 중복이면 아무것도 안 만듦 in_place_type<sqlite_text>: variant
                    // 안에서 sqlite_text(std::string)를 바로 생성하라는 태그 →
                    // std::string(p, n) 호출: 시작 포인터 + 바이트 길이로 deep copy (\0
                    // 포함 가능)
                    map.try_emplace(key, std::in_place_type<sqlite_text>, p, n);
                } else {
                    map.try_emplace(key, sqlite_text{});
                }
                break;
            }
            case SQLITE_BLOB: {
                const auto* p =
                    static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, i));
                const auto n = static_cast<std::size_t>(sqlite3_column_bytes(stmt, i));
                if (p) {

                    map.try_emplace(key, std::in_place_type<sqlite_blob>, p, p + n);
                } else {
                    map.try_emplace(key, sqlite_blob{});
                }
                break;
            }
            case SQLITE_NULL:
                map.try_emplace(key, sqlite_null{});
                break;
            default:
                // 지정된 타입이 아니면 에러를 발생하고 더 이상 추출하지 않는다.
                return ctls::failed("unknown column type: index={}", i);
            }
        }

        return sqlite_row{std::move(map)};
    }

} // namespace csql
