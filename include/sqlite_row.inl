#pragma once
#include <ctls/ctls.hpp>
#include <cstdint>
#include <optional>
#include <variant>

#include "types.hpp"

namespace csql {
    template <sqlite_value_type T>
    std::optional<T> sqlite_row::get(const std::size_t index) const {
        if (_values.size() <= index) {
            ILOG_ERROR("out of range");
            return std::nullopt;
        }

        if (std::holds_alternative<sqlite_null>(_values[index])) {
            return std::nullopt;
        }

        // double로 읽는데 저장된 값이 정수면 변환해서 돌려줌
        if constexpr (std::same_as<T, sqlite_double>) {
            if (const auto* i = std::get_if<sqlite_int64>(&_values[index])) {
                return static_cast<sqlite_double>(*i);
            }
        }

        const auto* value = std::get_if<T>(&_values[index]);

        if (!value) {
            ILOG_ERROR("type mismatch");
            return std::nullopt;
        }

        return *value;
    }
}