#include <mutex>
#include <cstdint>
#include <sqlite3.h>
#include <deque>
#include <string>
#include <ctls/ctls.hpp>
#include <memory>
#include <vector>
#include <algorithm>
#include <utility>
#include <filesystem>

#include "csql/types.hpp"
#include "csql/manager.hpp"
#include "csql/connection.hpp"
#include "csql/client.hpp"

namespace csql {

    manager::~manager() {
        std::unique_lock lock{_mutex};
        // 초기화에 실패했으면 커넥션 정리할 필요가 없음. 바로 종료
        // 빌려준 포인터들도 없으니 바로 종료 가능
        if (_status == e_manager_status::terminated) {
            return;
        }

        _status = e_manager_status::terminating;
        _is_callable.notify_all();
        _is_callable.wait(lock, [this] {
            return _idle_connections.size() == _max_connections && _wait_connection == 0;
        });

        // 모두 회수가 되었다면 커넥션을 정리한다.
        _idle_connections.clear();
        _status = e_manager_status::terminated;
    }

    bool manager::init(
        const std::filesystem::path& filepath,
        const std::size_t max_connections,
        const int wait_busy_milli) {
        std::unique_lock lock{_mutex};
        if (_status != e_manager_status::init) {
            ILOG_ERROR("manager already initialized");
            return false;
        }

        if (max_connections == 0) {
            _status = e_manager_status::terminated;
            ILOG_ERROR("max_connections must be greater than 0");
            return false;
        }

        if (wait_busy_milli < 0) {
            ILOG_ERROR("wait_busy_milli must be greater than or equal to 0");
            _status = e_manager_status::terminated;
            return false;
        }


        _status = e_manager_status::connecting;
        _max_connections = max_connections;
        _wait_busy_milli = wait_busy_milli;
        lock.unlock();

        // flag
        constexpr int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

        for (std::size_t i = 0; i < max_connections; ++i) {
            sqlite3* db = nullptr;
            if (sqlite3_open_v2(filepath.string().c_str(), &db, flags, nullptr) !=
                SQLITE_OK) [[unlikely]] {
                ILOG_ERROR(
                    "failed to open database: path={}, error={}", filepath.string(),
                    sqlite3_errmsg(db));
                sqlite3_close_v2(db);
                break;
            }

            // db 기본값 초기화 하기
            if (!init_db(db)) {
                sqlite3_close_v2(db);
                break;
            }

            _idle_connections.push_back(sqlite3_ptr{db});
        }

        // 모두 초기화 되지 않았다면 만들어졌던 커넥션을 모두 소거하고 진행을 멈춘다
        if (_idle_connections.size() != _max_connections) {
            _idle_connections.clear();
            lock.lock();
            _max_connections = 0; // 연결된 커넥션 없음
            _status = e_manager_status::terminated;
            return false;
        }

        lock.lock();
        _status = e_manager_status::ready;
        return true;
    }

    std::unique_ptr<client> manager::get_client() {
        std::unique_lock lock{_mutex};
        if (_status != e_manager_status::ready) {
            ILOG_WARN("manager is not ready");
            return nullptr;
        }

        // 대기하는 프로세스 수를 세어야 소멸자에서 판별이 가능
        _wait_connection += 1;
        _is_callable.wait(lock, [this] {
            return !_idle_connections.empty() || _status == e_manager_status::terminating;
        });
        _wait_connection -= 1;

        if (_status == e_manager_status::terminating) {
            _is_callable.notify_all();
            ILOG_ERROR("manager is terminating");
            return nullptr;
        }

        auto sqlite_db = std::move(_idle_connections.front());
        _idle_connections.pop_front();

        auto ptr = sqlite_db.get();
        _in_progress_connections.emplace_back(std::move(sqlite_db));

        lock.unlock(); // 데이터 처리가 끝났으면 바로 락을 푸는것이 좋다.

        return std::make_unique<client>(
            std::make_shared<connection>(ptr, [this](sqlite3* db) { release(db); }));
    }

    void manager::release(sqlite3* db) {
        std::unique_lock lock{_mutex};

        const auto it =
            std::ranges::find_if(_in_progress_connections, [raw = db](auto& elem) {
                return elem.get() == raw;
            });

        if (it == _in_progress_connections.end()) {
            ILOG_ERROR("invalid db pointer");
            return;
        }

        _idle_connections.push_back(std::move(*it));
        *it = std::move(_in_progress_connections
                            .back()); // 제일 마지막 인자를 빈 위치로 이동 ->
                                      // 위치이동이므로 배열 전체가 자리이동하지 않는다
        _in_progress_connections.pop_back(); // 마지막 인자 빼기

        _is_callable.notify_one();
    }

    bool manager::init_db(sqlite3* db) const {
        if (sqlite3_busy_timeout(db, _wait_busy_milli) != SQLITE_OK) {
            ILOG_ERROR("failed to set busy timeout");
            return false;
        }

        char* err = nullptr;
        const auto res = sqlite3_exec(
            db,
            "PRAGMA journal_mode=WAL;"
            "PRAGMA foreign_keys=ON;"
            "PRAGMA synchronous=NORMAL;",
            nullptr, nullptr, &err);
        if (res != SQLITE_OK) {
            std::string message = err ? err : "unknown error";
            ILOG_ERROR("failed to init db: error={}", message);
            sqlite3_free(err);
            return false;
        }
        return true;
    }

} // namespace sql
