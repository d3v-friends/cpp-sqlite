#pragma once
#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <ctls/ctls.hpp>

#include "types.hpp"

namespace csql {
    // concept bool 판별식을 통해 타입체크 도와주는 기능
    // 컴파일 시기에 결정
    template <class T>
    concept sqlite_value_type = types::is_variant_member<T, sqlite_values>::value;

    // 쿼리는 이미 작성되어 있으므로 개발자가 순서와 타입을 알수 있다.
    class sqlite_row {
    public:
        explicit sqlite_row() = default;

        explicit sqlite_row(std::vector<sqlite_values> values)
            : _values(std::move(values)) {}

        ~sqlite_row() = default;

        sqlite_row(const sqlite_row&) = delete;

        sqlite_row& operator=(const sqlite_row&) = delete;

        sqlite_row(sqlite_row&&) = default;

        sqlite_row& operator=(sqlite_row&&) = default;

        template <sqlite_value_type T> ctls::result<T> get(std::size_t index) const;

        [[nodiscard]] bool is_null(const std::size_t index) const {
            return index < _values.size() &&
                   std::holds_alternative<sqlite_null>(_values[index]);
        }

        [[nodiscard]] bool is_empty() const {
            return _values.empty();
        }

    private:
        std::vector<sqlite_values> _values;
    };

} // namespace csql

#include "sqlite_row.inl"
