#pragma once
#include "connection.hpp"

#include <optional>
#include <memory>
#include <ranges>
#include <type_traits>
#include <cstdint>
#include <string_view>
#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "cursor.hpp"
#include "types.hpp"

namespace csql {
    template <typename... ARGS>
    std::unique_ptr<cursor> client::query(const std::string& sql, ARGS&&... args) {
        if (sql.empty()) [[unlikely]] {
            ILOG_ERROR("sql is empty");
            return nullptr;
        }

        const auto db = _conn->get_db();
        sqlite3_stmt* stmt = nullptr;
        const auto res = sqlite3_prepare_v2(
            db, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);

        if (res != SQLITE_OK) {
            ILOG_ERROR(
                "failed to prepare sql: sql={}, error={}", sql, sqlite3_errmsg(db));
            return nullptr;
        }

        if (int i = 0; !((bind_stmt(stmt, ++i, args) == SQLITE_OK) && ...)) {
            ILOG_ERROR(
                "failed to bind: sql={}, index={}, error={}", sql, i, sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return nullptr;
        }

        return std::make_unique<cursor>(_conn, sqlite3_stmt_ptr{stmt});
    }

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
    template <class T> int client::bind_stmt(sqlite3_stmt* stmt, int i, T& value) {
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
                stmt, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        } else if constexpr (byte_range<U>) {
            return sqlite3_bind_blob(
                stmt, i, std::ranges::data(value),
                static_cast<int>(std::ranges::size(value)), SQLITE_TRANSIENT);
        } else {
            static_assert(sizeof(U) == 0, "unsupported type");
            return SQLITE_ERROR;
        }
    }
} // namespace csql