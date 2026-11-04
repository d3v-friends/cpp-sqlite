#pragma once
#include <utility>
#include "types.hpp"

namespace sql {

    // stmt 소유는 cursor 가 하되, 클라이언트가 소멸되면 cursor_base 자체를 소멸시켜서
    // 추가 사용을 방지한다. client 가 생성된 곳에서만 사용가능 하도록 구성 함
    class cursor_base {
    public:
        explicit cursor_base(const cursor_id id, sqlite3_stmt_ptr stmt)
            : _id{id}, _stmt{std::move(stmt)} {}
        virtual ~cursor_base() = default;
        cursor_base(const cursor_base&) = delete;
        cursor_base& operator=(const cursor_base&) = delete;
        cursor_base(cursor_base&&) = delete;
        cursor_base& operator=(cursor_base&&) = delete;

        [[nodiscard]] cursor_id get_id() const noexcept { return _id; }

    private:
        const cursor_id _id;

    protected:
        sqlite3_stmt_ptr _stmt;
    };

} // namespace sql
