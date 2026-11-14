#pragma once
#include <concepts>
#include <variant>

#include <ctls/ctls.hpp>

#include "csql/types.hpp"

namespace csql {
    template <sqlite_value_type T>
    ctls::result<T> sqlite_row::get(const std::string& key) const {
        const auto it = _data.find(key);
        if (it == _data.end()) {
            return ctls::failed("key not found");
        }

        if (std::holds_alternative<sqlite_null>(it->second)) {
            if constexpr (std::same_as<T, sqlite_null>) {
                return sqlite_null{};
            } else {
                return ctls::failed("value is null: key={}", key);
            }
        }
        // double로 읽는데 저장된 값이 정수면 변환해서 돌려줌
        if constexpr (std::same_as<T, sqlite_double>) {
            if (const auto* i = std::get_if<sqlite_int64>(&it->second)) {
                return static_cast<sqlite_double>(*i);
            }
        }

        const auto* value = std::get_if<T>(&it->second);

        if (!value) {
            return ctls::failed("type mismatch");
        }

        return *value;
    }
} // namespace csql
