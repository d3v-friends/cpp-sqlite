#pragma once
#include <concepts>
#include <string>
#include <utility>
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

    template <sqlite_bindable T>
    ctls::result<> sqlite_row::bind(const std::string& key, T& out) const {
        if constexpr (sqlite_optional_value_type<T>) {
            using V = typename T::value_type;

            const auto it = _data.find(key);
            if (it == _data.end()) {
                return ctls::failed("key not found: key={}", key);
            }
            // NULL 이면 실패가 아니라 비어 있는 optional
            if (std::holds_alternative<sqlite_null>(it->second)) {
                out.reset();
                return {};
            }

            auto r = get<V>(key);
            if (!r) {
                return std::move(r); // result_base&& 로 실패 그대로 전달
            }
            out = std::move(r).operator*();
            return {};
        } else {
            // 일반 타입: NULL 이면 get 이 "value is null" 실패를 돌려줌
            auto r = get<T>(key);
            if (!r) {
                return std::move(r);
            }
            out = std::move(r).operator*();
            return {};
        }
    }
} // namespace csql
