#pragma once
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include "types.hpp"

namespace sql {
    class db;
    template <class T> class cursor;
    class cursor_base;

    class client {
    public:
        explicit client(db* db) : _db{db} {}
        ~client();
        client(const client&) = delete;
        client& operator=(const client&) = delete;
        client(client&&) = delete;
        client& operator=(client&&) = delete;

        // 쿼리 실행만 하기
        [[nodiscard]] bool exec(const std::string& sql) const;

        // 모든 커서는 client 보다 오래 유지되지 않는다.
        // client 소멸시 발급한 모든 cursor 소멸함
        template <class T, typename... ARGS>
        [[nodiscard]] cursor<T>* query(const std::string& sql, ARGS&&... args);

        // 미리 사용 해제 하기
        [[nodiscard]] bool release_cursor(cursor_id id);

    private:
        mutable std::mutex _mutex;
        std::map<cursor_id, std::unique_ptr<cursor_base>> _queries;
        cursor_id _cursor_id = 0;
        db* _db;
    };

} // namespace sql

#include "client.inl"