#pragma once
#include <utility>
#include <mutex>

#include "cursor.hpp"
#include "db.hpp"

namespace sql {
    template <class T, typename... ARGS>
    cursor<T>* client::query(const std::string& sql, ARGS&&... args) {
        std::unique_lock lock{_mutex};
        _cursor_id += 1;
        auto id = _cursor_id;
        lock.unlock();

        auto stmt = _db->query(sql, std::forward<ARGS>(args)...);
        if (stmt == nullptr) {
            return nullptr;
        }

        auto cur = std::make_unique<cursor<T>>(id, std::move(stmt));
        auto ptr = cur.get();

        lock.lock();
        _queries.try_emplace(id, std::move(cur));
        lock.unlock();

        return ptr;
    }
} // namespace sql