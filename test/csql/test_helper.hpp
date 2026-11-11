#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include <sqlite3.h>

#include "csql/database.hpp"

namespace csql::test {
    // 테스트용 임시 db 파일. 소멸 시 -wal, -shm 까지 지운다.
    // 테스트 실행 폴더(gtest_discover_tests 의 WORKING_DIRECTORY)에 만든다.
    class temp_db_file {
    public:
        temp_db_file() {
            static std::atomic<unsigned> counter{0};
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            _path = std::filesystem::current_path() /
                    ("csql_test_" + std::to_string(stamp) + "_" +
                     std::to_string(counter++) + ".db");
        }

        ~temp_db_file() {
            std::error_code ec;
            std::filesystem::remove(_path, ec);
            std::filesystem::remove(_path.string() + "-wal", ec);
            std::filesystem::remove(_path.string() + "-shm", ec);
        }

        temp_db_file(const temp_db_file&) = delete;
        temp_db_file& operator=(const temp_db_file&) = delete;
        temp_db_file(temp_db_file&&) = delete;
        temp_db_file& operator=(temp_db_file&&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return _path;
        }

    private:
        std::filesystem::path _path;
    };

    // manager 없이 쓰는 메모리 db. 반납 콜백에서 바로 닫는다.
    inline std::shared_ptr<database> open_memory_db() {
        sqlite3* raw = nullptr;
        if (sqlite3_open(":memory:", &raw) != SQLITE_OK) {
            sqlite3_close_v2(raw);
            return nullptr;
        }
        return std::make_shared<database>(raw, [](sqlite3* db) { sqlite3_close_v2(db); });
    }
} // namespace csql::test
