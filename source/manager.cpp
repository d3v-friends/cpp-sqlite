#include <mutex>
#include <ctls/ctls.hpp>
#include <sqlite3.h>
#include <memory>

#include "manager.hpp"
#include "client.hpp"
#include "types.hpp"
#include "db.hpp"

namespace sql {

    manager::~manager() {
        std::unique_lock lock{_mutex};
        _status = e_manager_status::terminated;

        _notify_full.notify_all();
        _notify_full.wait(
            lock, [this] { return _wait_db == 0 && _idle_db == _max_connections; });
    }

    bool manager::connect() {
        std::unique_lock lock{_mutex};
        if (_status != e_manager_status::init) [[unlikely]] {
            ILOG_ERROR("manager is not init: status={}", to_string(_status));
            return false;
        }
        _status = e_manager_status::connecting;
        lock.unlock();

        int flags = SQLITE_OPEN_NOMUTEX; // 클라이언트에서 동시성 관리 태그
        flags |= SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

        _dbs.reserve(_max_connections);
        for (std::size_t i = 0; i < _max_connections; ++i) {
            sqlite3* raw = nullptr;
            if (sqlite3_open_v2(_db_path.c_str(), &raw, flags, nullptr) != SQLITE_OK)
                [[unlikely]] {
                // 모든 객체 삭제
                ILOG_ERROR(
                    "failed to open database: path={}, error={}", _db_path.string(),
                    sqlite3_errmsg(raw));
                sqlite3_close_v2(raw);
                _dbs.clear();

                lock.lock();
                _status = e_manager_status::terminated;
                return false;
            }

            _dbs.emplace_back(std::make_unique<db>(sqlite3_ptr{raw}, this));

            // 초기화 실패하면 모두 삭제하고 종료한다
            if (!_dbs[i]->init()) [[unlikely]] {
                _dbs.clear();
                lock.lock();
                _status = e_manager_status::terminated;
                return false;
            }
        }

        lock.lock();
        _status = e_manager_status::ready;
        return true;
    }

    std::unique_ptr<client> manager::get_client() {
        std::unique_lock lock{_mutex};
        if (_status != e_manager_status::ready) [[unlikely]] {
            ILOG_ERROR("manager is not ready");
            return nullptr;
        }

        // 커넥션풀이 생겼거나 프로그램이 종료 되었을때 깨어난다
        // 종료할때 작업중인 모든 요청을 확인하기 위해서 _wait_db 로 체크해준다.
        _wait_db += 1;
        _notify_full.wait(lock, [this] {
            return 0 < _idle_db || _status == e_manager_status::terminated;
        });
        _wait_db -= 1;

        // 기다렸다가 호출되었는데 종료되었으면 실패로 반납
        if (_status == e_manager_status::terminated) [[unlikely]] {
            _notify_full.notify_all(); // 대기에서 빠져나갔다고 소멸자한테 알림
            ILOG_ERROR("manager is terminated");
            return nullptr;
        }

        _idle_db -= 1;

        for (const auto& db : _dbs) {
            if (!db->can_acquire()) {
                continue;
            }
            return std::make_unique<client>(db.get());
        }

        ILOG_ERROR("failed get client");
        return nullptr;
    }

    void manager::release() {
        std::scoped_lock lock{_mutex};
        _idle_db += 1;
        const bool is_closing = _status == e_manager_status::terminated;
        is_closing ? _notify_full.notify_all() : _notify_full.notify_one();
    }

} // namespace sql
