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

    namespace types {
        template <class T> struct is_optional : std::false_type {};
        template <class T> struct is_optional<std::optional<T>> : std::true_type {};
    } // namespace types

    // std::optional<sqlite_int64> 같은 것. optional<sqlite_null>은 제외
    // && 는 앞에서 거짓이면 뒤를 검사하지 않아서 T::value_type 이 없어도 안전
    template <class T>
    concept sqlite_optional_value_type =
        types::is_optional<T>::value && sqlite_value_type<typename T::value_type> &&
        !std::same_as<typename T::value_type, sqlite_null>;

    template <class T>
    concept sqlite_bindable = sqlite_value_type<T> || sqlite_optional_value_type<T>;

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

        ctls::result<sqlite_null> is_null(const std::string& key) const {
            const auto it = _data.find(key);
            if (it == _data.end()) {
                return ctls::failed("key not found");
            }

            if (!std::holds_alternative<sqlite_null>(it->second)) {
                return ctls::failed("value is not null: key={}", key);
            }

            return sqlite_null{};
        }

        template <sqlite_bindable T>
        ctls::result<> bind(const std::string& key, T& out) const;

        [[nodiscard]] bool is_empty() const {
            return _data.empty();
        }

    private:
        std::map<std::string, sqlite_values> _data;
    };

} // namespace csql

#include "sqlite_row.inl"
