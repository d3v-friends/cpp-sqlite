#pragma once
#include <mutex>
#include <optional>

#include "types.hpp"
#include "cursor_base.hpp"

namespace sql {
    template <class T> class cursor : public cursor_base {
    public:
        explicit cursor(const cursor_id id, sqlite3_stmt_ptr stmt)
            : cursor_base{id, std::move(stmt)} {}
        ~cursor() override = default;
        cursor(const cursor&) = delete;
        cursor& operator=(const cursor&) = delete;
        cursor(cursor&&) = delete;
        cursor& operator=(cursor&&) = delete;

        // 조회된 것을 하나씩 가저온다.
        std::optional<T> next();

    private:
        mutable std::mutex _mutex;
    };

} // namespace sql

#include "cursor.inl"