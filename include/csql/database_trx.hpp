#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <ctls/ctls.hpp>

#include "cursor_trx.hpp"
#include "database.hpp"

namespace csql {
    class database_trx {
    public:
        explicit database_trx(
            std::shared_ptr<database> db, std::unique_lock<std::mutex>& lock)
            : _lock{lock}, _db{std::move(db)} {}
        ~database_trx() = default;
        database_trx(const database_trx&) = delete;
        database_trx operator=(const database_trx&) = delete;
        database_trx(database_trx&&) = delete;
        database_trx& operator=(database_trx&&) = delete;

        template <typename... ARGS>
        ctls::result<> exec(const std::string& sql, ARGS&&... args) {
            return _db->exec(_lock, sql, std::forward<ARGS>(args)...);
        }

        template <typename... ARGS>
        ctls::result<cursor_trx> query(const std::string& sql, ARGS&&... args) {
            auto cursor = _db->query(_lock, sql, std::forward<ARGS>(args)...);
            if (!cursor) {
                return cursor.extract_failed();
            }
            return cursor_trx{_lock, std::move(*cursor)};
        }

    private:
        friend class manager;
        std::unique_lock<std::mutex>& _lock;
        std::shared_ptr<database> _db;

        ctls::result<> rollback(std::unique_lock<std::mutex>& lock);
        ctls::result<> commit(std::unique_lock<std::mutex>& lock);
        ctls::result<> begin();
    };
} // namespace csql
