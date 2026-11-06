#include "sqlite_row.hpp"

namespace csql {
    bool sqlite_row::is_null(const std::size_t index) const {
        return index < _values.size() &&
               std::holds_alternative<sqlite_null>(_values[index]);
    }
} // namespace csql