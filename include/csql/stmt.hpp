#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

namespace csql {
    inline int bind_text(sqlite3_stmt* stmt, const int i, const std::string_view s) {
        // data가 null인 빈 string_view를 그대로 넘기면 SQLite가 NULL로 저장하므로 ""로 바꾼다
        return sqlite3_bind_text64(
            stmt, i, s.data() ? s.data() : "", static_cast<sqlite3_uint64>(s.size()),
            SQLITE_TRANSIENT, SQLITE_UTF8);
    }

    /*
     * if constexpr 문법 (C++17)
     *  조건을 컴파일 타임에 계산해서 true인 분기만 남기고, 나머지 분기는 버린다.
     *  조건이 템플릿 파라미터(T)에 의존할 때만 버려진 분기의 타입 검사를 건너뛴다.
     *  서로 다른 T마다 함수가 하나씩 생기고 각 함수에는 그 T에 맞는 분기만 들어간다.
     *  여기서 리터럴 "string"은 갯수마다 타입이 달라져서 각각 생기게 된다.
     */
    template <class T> inline constexpr bool always_false_v = false;

    template <class T> struct is_optional : std::false_type {};
    template <class T> struct is_optional<std::optional<T>> : std::true_type {};

    // 문자 타입: 숫자로 저장되면 의도와 다르므로 막는다 (uint8_t, int8_t는 숫자로 허용)
    template <class T>
    concept char_like = std::same_as<T, char> || std::same_as<T, wchar_t> ||
                        std::same_as<T, char8_t> || std::same_as<T, char16_t> ||
                        std::same_as<T, char32_t>;

    // blob: uint8_t 또는 std::byte의 연속 메모리 (vector, array, span 등)
    template <class T>
    concept byte_range = std::ranges::contiguous_range<T> &&
                         std::ranges::sized_range<T> &&
                         (std::same_as<std::ranges::range_value_t<T>, std::uint8_t> ||
                          std::same_as<std::ranges::range_value_t<T>, std::byte>);

    /*
     * 바인딩 규칙
     *  nullptr, 빈 optional, null인 const char*  -> NULL
     *  bool, 정수, enum                           -> INTEGER (int64)
     *  float, double                              -> REAL
     *  const char*, std::string, string_view, 리터럴 -> TEXT
     *  uint8_t / std::byte 연속 범위             -> BLOB
     *  char 계열, 64비트 이상 unsigned            -> 컴파일 에러
     */
    template <class T> int bind_stmt(sqlite3_stmt* stmt, const int i, const T& value) {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<U, std::nullptr_t>) {
            return sqlite3_bind_null(stmt, i);
        } else if constexpr (is_optional<U>::value) {
            if (!value.has_value()) {
                return sqlite3_bind_null(stmt, i);
            }
            return bind_stmt(stmt, i, *value);
        } else if constexpr (std::is_enum_v<U>) {
            // 기반 타입 규칙을 그대로 따른다 (enum : uint64_t 이면 막힘)
            return bind_stmt(stmt, i, static_cast<std::underlying_type_t<U>>(value));
        } else if constexpr (std::is_same_v<U, bool>) {
            return sqlite3_bind_int64(stmt, i, value ? 1 : 0);
        } else if constexpr (std::is_integral_v<U>) {
            static_assert(!char_like<U>, "char is not allowed, use std::string or an integer type");
            static_assert(
                !(std::is_unsigned_v<U> && sizeof(U) >= sizeof(sqlite3_int64)),
                "64-bit unsigned may overflow int64, cast to int64_t explicitly");
            return sqlite3_bind_int64(stmt, i, static_cast<sqlite3_int64>(value));
        } else if constexpr (std::is_floating_point_v<U>) {
            return sqlite3_bind_double(stmt, i, static_cast<double>(value));
        } else if constexpr (
            std::is_pointer_v<U> && std::is_convertible_v<U, const char*>) {
            if (value == nullptr) {
                return sqlite3_bind_null(stmt, i); // nullptr은 NULL로 저장
            }
            return bind_text(stmt, i, std::string_view{value});
        } else if constexpr (std::is_convertible_v<const U&, std::string_view>) {
            return bind_text(stmt, i, std::string_view{value});
        } else if constexpr (byte_range<U>) {
            if (std::ranges::empty(value)) {
                return sqlite3_bind_zeroblob(stmt, i, 0); // 길이 0 blob (NULL 아님)
            }
            return sqlite3_bind_blob64(
                stmt, i, std::ranges::data(value),
                static_cast<sqlite3_uint64>(std::ranges::size(value)), SQLITE_TRANSIENT);
        } else {
            static_assert(always_false_v<U>, "unsupported bind type");
            return SQLITE_ERROR;
        }
    }
} // namespace csql
