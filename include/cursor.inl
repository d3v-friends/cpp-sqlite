#pragma once
#include <optional>
#include <sqlite3.h>

namespace csql {
    template <class T> cursor<T>::~cursor() = default;

    template <class T> std::optional<T> cursor<T>::next() {
        std::scoped_lock lock{_conn->get_mutex()};

        if (sqlite3_step(_stmt.get()) == SQLITE_ROW) {
        }

        return std::nullopt;
    }

} // namespace csql