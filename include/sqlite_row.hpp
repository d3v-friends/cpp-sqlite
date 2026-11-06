#pragma once
#include <ctls/ctls.hpp>
#include <cstddef>
#include <vector>
#include <variant>
#include <optional>
#include <utility>

#include "types.hpp"

namespace csql {
    // concept bool 판별식을 통해 타입체크 도와주는 기능
    // 컴파일 시기에 결정
    template <class T>
    concept sqlite_value_type = types::is_variant_member<T, sqlite_values>::value;

    // 쿼리는 이미 작성되어 있으므로 개발자가 순서와 타입을 알수 있다.
    class sqlite_row {
    public:
        explicit sqlite_row(std::vector<sqlite_values> values)
            : _values(std::move(values)) {}
        ~sqlite_row() = default;
        sqlite_row(const sqlite_row&) = delete;
        sqlite_row& operator=(const sqlite_row&) = delete;
        sqlite_row(sqlite_row&&) = default;
        sqlite_row& operator=(sqlite_row&&) = default;

        template <sqlite_value_type T> std::optional<T> get(std::size_t index) const;

        [[nodiscard]] bool is_null(std::size_t index) const;

    private:
        std::vector<sqlite_values> _values;
    };

} // namespace csql

#include "sqlite_row.inl"