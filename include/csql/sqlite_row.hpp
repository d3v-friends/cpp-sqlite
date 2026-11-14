#pragma once
#include <concepts>
#include <map>
#include <optional>
#include <utility>
#include <variant>

#include <ctls/ctls.hpp>

#include "types.hpp"

namespace csql {
    // concept bool 판별식을 통해 타입체크 도와주는 기능
    // 컴파일 시기에 결정
    template <class T>
    concept sqlite_value_type = types::is_variant_member<T, sqlite_values>::value;
    class sqlite_row {
    public:
        explicit sqlite_row() = default;

        explicit sqlite_row(std::map<std::string, sqlite_values> data)
            : _data(std::move(data)) {}

        ~sqlite_row() = default;

        sqlite_row(const sqlite_row&) = delete;

        sqlite_row& operator=(const sqlite_row&) = delete;

        sqlite_row(sqlite_row&&) = default;

        sqlite_row& operator=(sqlite_row&&) = default;

        template <sqlite_value_type T> ctls::result<T> get(const std::string& key) const;

        [[nodiscard]] bool is_empty() const {
            return _data.empty();
        }

    private:
        std::map<std::string, sqlite_values> _data;
    };

} // namespace csql

#include "sqlite_row.inl"
