#include "csql/cursor_trx.hpp"

#include <ctls/ctls.hpp>

#include "csql/cursor.hpp"
#include "csql/sqlite_row.hpp"

namespace csql {

    ctls::result<sqlite_row> cursor_trx::next() {
        return _cursor.next(_lock);
    }

    bool cursor_trx::is_end() const noexcept {
        return _cursor.is_end();
    }
} // namespace csql
