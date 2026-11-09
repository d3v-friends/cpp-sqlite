#pragma once
#include <ctls/ctls.hpp>
#include <variant>
#include <concepts>
#include <cstddef>

#include "types.hpp"

namespace csql {
    template <sqlite_value_type T>
    ctls::result<T> sqlite_row::get(const std::size_t index) const {
        if (_values.size() <= index) {
            return ctls::failed("out of range");
        }

        if (std::holds_alternative<sqlite_null>(_values[index])) {
            return ctls::failed("null");
        }

        // double로 읽는데 저장된 값이 정수면 변환해서 돌려줌
        if constexpr (std::same_as<T, sqlite_double>) {
            if (const auto* i = std::get_if<sqlite_int64>(&_values[index])) {
                return static_cast<sqlite_double>(*i);
            }
        }

        const auto* value = std::get_if<T>(&_values[index]);

        if (!value) {
            return ctls::failed("type mismatch");
        }

        return *value;
    }
}