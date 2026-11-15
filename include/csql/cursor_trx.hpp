#pragma once
#include <mutex>

#include <ctls/ctls.hpp>

#include "cursor.hpp"

namespace csql {
    class sqlite_row;

    class cursor_trx {
    public:
        explicit cursor_trx(std::unique_lock<std::mutex>& lock, cursor&& cursor)
            : _lock{lock}, _cursor{std::move(cursor)} {}
        ~cursor_trx() = default;
        cursor_trx(const cursor_trx&) = delete;
        cursor_trx operator=(const cursor_trx&) = delete;
        cursor_trx(cursor_trx&&) = default;
        cursor_trx& operator=(cursor_trx&&) = delete;

        ctls::result<sqlite_row> next();

        [[nodiscard]] bool is_end() const noexcept;

    private:
        std::unique_lock<std::mutex>& _lock;
        cursor _cursor;
    };
} // namespace csql
