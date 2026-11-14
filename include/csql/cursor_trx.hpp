#pragma once

#include "cursor.hpp"
#include "sqlite_row.hpp"

namespace csql {
    class cursor_trx {
    public:
        explicit cursor_trx(
            std::unique_lock<std::mutex>& lock, std::unique_ptr<cursor> cursor)
            : _lock{lock}, _cursor{std::move(cursor)} {}
        ~cursor_trx() = default;
        cursor_trx(const cursor_trx&) = delete;
        cursor_trx operator=(const cursor_trx&) = delete;
        cursor_trx(cursor_trx&&) = delete;
        cursor_trx& operator=(cursor_trx&&) = delete;

        ctls::result<sqlite_row> next() const {
            return _cursor->next(_lock);
        }

        [[nodiscard]] bool is_end() const noexcept {
            return _cursor->is_end();
        }

    private:
        std::unique_lock<std::mutex>& _lock;
        std::unique_ptr<cursor> _cursor;
    };
} // namespace csql
