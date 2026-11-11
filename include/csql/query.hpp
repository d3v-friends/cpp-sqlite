#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "cursor.hpp"
#include "database.hpp"
#include "sqlite_row.hpp"
#include "types.hpp"

namespace csql {
    /*
     * if constexpr 문법 (C++17)
     *  조건을 컴파일 타임에 계산해서 true인 분기만 남기고, 나머지 분기는 버린다.
     *  조건이 템플릿 파라미터(T)에 의존할 때만 버려진 분기의 타입 검사를 건너뛴다.
     *  서로 다른 T마다 함수가 하나씩 생기고 각 함수에는 그 T에 맞는 분기만 들어간다.
     *  여기서 리터럴 "string"은 갯수마다 타입이 달라져서 각각 생기게 된다.
     */
    template <class T>
    concept byte_range = std::ranges::contiguous_range<T> &&
                         std::same_as<std::ranges::range_value_t<T>, std::uint8_t>;

    template <class T> int bind_stmt(sqlite3_stmt* stmt, const int i, const T& value) {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<U, std::nullptr_t>) {
            return sqlite3_bind_null(stmt, i);
        } else if constexpr (std::is_integral_v<U>) {
            return sqlite3_bind_int64(stmt, i, static_cast<sqlite3_int64>(value));
        } else if constexpr (std::is_floating_point_v<U>) {
            return sqlite3_bind_double(stmt, i, static_cast<double>(value));
        } else if constexpr (std::is_convertible_v<const U&, std::string_view>) {
            const std::string_view s = value;
            return sqlite3_bind_text(
                stmt, i, s.data() ? s.data() : "", static_cast<int>(s.size()),
                SQLITE_TRANSIENT);
        } else if constexpr (byte_range<U>) {
            if (std::ranges::empty(value)) {
                return sqlite3_bind_zeroblob(stmt, i, 0); // 길이 0 blob
            }
            return sqlite3_bind_blob(
                stmt, i, std::ranges::data(value),
                static_cast<int>(std::ranges::size(value)), SQLITE_TRANSIENT);
        } else {
            static_assert(sizeof(U) == 0, "unsupported type");
            return SQLITE_ERROR;
        }
    }

    template <typename... ARGS>
    ctls::result<std::unique_ptr<cursor>> query(
        std::shared_ptr<database> conn,
        bool could_lock,
        const std::string& sql,
        ARGS&&... args) {
        if (sql.empty()) [[unlikely]] {
            return ctls::failed("sql is empty");
        }

        auto* db = conn->get_sqlite3();
        sqlite3_stmt* stmt = nullptr;

        const char* tail = nullptr;

        const auto res = sqlite3_prepare_v2(
            db, sql.data(), static_cast<int>(sql.size()), &stmt, &tail);
        if (res != SQLITE_OK) {
            return ctls::failed(
                "failed to prepare sql: sql={}, error={}", sql, sqlite3_errmsg(db));
        }

        if (stmt == nullptr) {
            return ctls::failed("sql has no statement: sql={}", sql);
        }

        if (tail && std::string_view{tail}.find_first_not_of(" \t\r\n;") !=
                        std::string_view::npos) {
            sqlite3_finalize(stmt);
            return ctls::failed("multiple statements not supported: sql={}", sql);
        }

        if (int i = 0; !((bind_stmt(stmt, ++i, args) == SQLITE_OK) && ...)) {
            const auto message = std::format(
                "failed to bind: sql={}, index={}, error={}", sql, i, sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return ctls::failed(message);
        }

        return std::make_unique<cursor>(
            std::move(conn), sqlite3_stmt_ptr{stmt}, could_lock);
    }

    template <typename... ARGS>
    ctls::status exec(
        std::shared_ptr<database> conn,
        bool could_lock,
        const std::string& sql,
        ARGS&&... args) {
        auto cur = query(conn, could_lock, sql, std::forward<ARGS>(args)...);
        if (!cur) {
            return cur.to_failed();
        }

        while (true) {
            auto r = (*cur)->next();
            if (!r) {
                return r.to_failed();
            }
            if (r->is_empty()) {
                return ctls::ok(); // DONE
            }
        }
    }

    inline ctls::result<sqlite_row> step_stmt(sqlite3* db, sqlite3_stmt* stmt) {
        switch (sqlite3_step(stmt)) {
        case SQLITE_DONE:
            return sqlite_row{};
        case SQLITE_ROW:
            break;
        default:
            return ctls::failed("failed step stmt: error={}", sqlite3_errmsg(db));
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
                const auto* p =
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                const auto n = static_cast<std::size_t>(sqlite3_column_bytes(stmt, i));
                if (p) {
                    values.emplace_back(std::in_place_type<sqlite_text>, p, n);
                } else {
                    values.emplace_back(sqlite_text{});
                }
                break;
            }
            case SQLITE_BLOB: {
                const auto* p =
                    static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, i));
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
                return ctls::failed("unknown column type: index={}", i);
            }
        }

        return sqlite_row{std::move(values)};
    }

} // namespace csql
