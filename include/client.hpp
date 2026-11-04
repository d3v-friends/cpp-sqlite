#pragma once
#include <types.hpp>
#include <string>

namespace sql {
    class db;
    class client {
    public:
        explicit client(db* db) : _db{db} {}
        ~client();
        client(const client&) = delete;
        client& operator=(const client&) = delete;
        client(client&&) = delete;
        client& operator=(client&&) = delete;

        [[nodiscard]] bool exec(const std::string& sql) const;

    private:
        db* _db;
    };

} // namespace sql
