#include <optional>

#include "cursor.hpp"
#include "connection.hpp"
#include "sqlite_row.hpp"

namespace csql {
    cursor::~cursor() = default;

    std::optional<sqlite_row> cursor::next() {
        std::scoped_lock lock{_conn->get_mutex()};
        // todo 작성중
        return std::nullopt;
    }
} // namespace csql