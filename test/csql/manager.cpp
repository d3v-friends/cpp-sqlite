// manager 테스트
// 위치: source/manager_test.cpp (cmake/test.cmake 가 source/*_test.cpp 를 수집함)
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sqlite3.h>

#include "csql/manager.hpp"
#include "csql/client.hpp"

using namespace std::chrono_literals;

namespace {
    // 블로킹 여부 판정용. 너무 짧으면 느린 머신에서 오탐 나니 넉넉하게
    constexpr auto k_block_check = 200ms;
    constexpr auto k_ready_limit = 3s;

    std::filesystem::path make_temp_db_path() {
        std::random_device rd;
        const auto name = "csql_manager_test_" + std::to_string(rd()) + ".db";
        return std::filesystem::temp_directory_path() / name;
    }

    // 다른 커넥션으로 열어서 PRAGMA 값 하나 읽기 (cursor 없이 검증하려고 raw API 사용)
    std::string read_pragma_text(const std::filesystem::path& path, const char* sql) {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) !=
            SQLITE_OK) {
            sqlite3_close_v2(db);
            return {};
        }
        std::string out;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK &&
            sqlite3_step(stmt) == SQLITE_ROW) {
            const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (p) {
                out.assign(p, static_cast<std::size_t>(sqlite3_column_bytes(stmt, 0)));
            }
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return out;
    }

    std::int64_t read_scalar_int(const std::filesystem::path& path, const char* sql) {
        sqlite3* db = nullptr;
        std::int64_t out = -1;
        if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
            SQLITE_OK) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK &&
                sqlite3_step(stmt) == SQLITE_ROW) {
                out = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close_v2(db);
        return out;
    }
} // namespace

class ManagerTest : public ::testing::Test {
protected:
    void SetUp() override { _path = make_temp_db_path(); }

    void TearDown() override {
        std::error_code ec;
        for (const auto* suffix : {"", "-wal", "-shm", "-journal"}) {
            std::filesystem::remove(_path.string() + suffix, ec);
        }
    }

    std::filesystem::path _path;
};

// ───────────── init ─────────────

TEST_F(ManagerTest, InitSucceeds) {
    csql::manager mgr;
    EXPECT_TRUE(mgr.init(_path, 3));
    EXPECT_TRUE(std::filesystem::exists(_path));
}

TEST_F(ManagerTest, InitTwiceFails) {
    csql::manager mgr;
    ASSERT_TRUE(mgr.init(_path, 2));
    EXPECT_FALSE(mgr.init(_path, 2));
}

TEST_F(ManagerTest, InitZeroConnectionsFails) {
    csql::manager mgr;
    EXPECT_FALSE(mgr.init(_path, 0));
    EXPECT_EQ(mgr.get_client(), nullptr);
}

TEST_F(ManagerTest, InitNegativeBusyTimeoutFails) {
    csql::manager mgr;
    EXPECT_FALSE(mgr.init(_path, 1, -1));
    EXPECT_EQ(mgr.get_client(), nullptr);
}

TEST_F(ManagerTest, InitInvalidPathFails) {
    csql::manager mgr;
    // SQLITE_OPEN_CREATE 는 디렉터리까지 만들어주지 않음
    const auto bad =
        std::filesystem::temp_directory_path() / "csql_no_such_dir_xyz" / "a.db";
    EXPECT_FALSE(mgr.init(bad, 2));
    EXPECT_EQ(mgr.get_client(), nullptr);
}

TEST_F(ManagerTest, ReinitAfterFailureIsRejected) {
    // 현재 구현: 실패하면 terminated 로 가서 재시도 불가
    csql::manager mgr;
    ASSERT_FALSE(mgr.init(_path, 0));
    EXPECT_FALSE(mgr.init(_path, 1));
}

TEST_F(ManagerTest, InitAppliesWalJournalMode) {
    csql::manager mgr;
    ASSERT_TRUE(mgr.init(_path, 1));
    // journal_mode=WAL 은 DB 파일에 영구 저장되므로 다른 커넥션에서 확인 가능
    EXPECT_EQ(read_pragma_text(_path, "PRAGMA journal_mode;"), "wal");
}

// ───────────── get_client ─────────────

TEST_F(ManagerTest, GetClientBeforeInitReturnsNull) {
    csql::manager mgr;
    EXPECT_EQ(mgr.get_client(), nullptr);
}

TEST_F(ManagerTest, ClientCanExecSql) {
    csql::manager mgr;
    ASSERT_TRUE(mgr.init(_path, 2));
    {
        auto cli = mgr.get_client();
        ASSERT_NE(cli, nullptr);
        EXPECT_TRUE(cli->exec("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT);"));
        EXPECT_TRUE(cli->exec("INSERT INTO t (v) VALUES ('a'), ('b');"));
        EXPECT_FALSE(cli->exec("INSERT INTO no_such_table VALUES (1);"));
        EXPECT_FALSE(cli->exec(""));
    }
    EXPECT_EQ(read_scalar_int(_path, "SELECT COUNT(*) FROM t;"), 2);
}

TEST_F(ManagerTest, ConnectionsShareSameDatabase) {
    csql::manager mgr;
    ASSERT_TRUE(mgr.init(_path, 2));
    auto a = mgr.get_client();
    auto b = mgr.get_client();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_TRUE(a->exec("CREATE TABLE t (id INTEGER);"));
    EXPECT_TRUE(b->exec("INSERT INTO t VALUES (1);")); // 다른 커넥션에서도 테이블 보임
}

TEST_F(ManagerTest, ReleasedConnectionIsReused) {
    csql::manager mgr;
    ASSERT_TRUE(mgr.init(_path, 1));

    for (int i = 0; i < 5; ++i) {
        auto fut = std::async(std::launch::async, [&] { return mgr.get_client(); });
        ASSERT_EQ(fut.wait_for(k_ready_limit), std::future_status::ready)
            << "반납된 커넥션을 다시 못 받음 (i=" << i << ")";
        auto cli = fut.get();
        ASSERT_NE(cli, nullptr);
        // cli 소멸 → connection 소멸 → manager::release
    }
}

TEST_F(ManagerTest, GetClientBlocksWhenPoolExhausted) {
    csql::manager mgr;
    ASSERT_TRUE(mgr.init(_path, 1));

    auto held = mgr.get_client();
    ASSERT_NE(held, nullptr);

    auto fut = std::async(std::launch::async, [&] { return mgr.get_client(); });
    EXPECT_EQ(fut.wait_for(k_block_check), std::future_status::timeout)
        << "풀이 비었는데 get_client 가 바로 리턴함";

    held.reset(); // 반납하면 대기자가 깨어나야 함
    ASSERT_EQ(fut.wait_for(k_ready_limit), std::future_status::ready);
    EXPECT_NE(fut.get(), nullptr);
}

TEST_F(ManagerTest, MovedClientKeepsConnection) {
    // client 를 move 해도 커넥션은 반납되지 않고 새 소유자가 들고 있어야 함
    csql::manager mgr;
    ASSERT_TRUE(mgr.init(_path, 1));

    auto first = mgr.get_client();
    ASSERT_NE(first, nullptr);
    auto moved = std::move(first);

    auto fut = std::async(std::launch::async, [&] { return mgr.get_client(); });
    EXPECT_EQ(fut.wait_for(k_block_check), std::future_status::timeout);

    moved.reset();
    ASSERT_EQ(fut.wait_for(k_ready_limit), std::future_status::ready);
    EXPECT_NE(fut.get(), nullptr);
}

TEST_F(ManagerTest, PoolNeverExceedsMaxConnections) {
    constexpr std::size_t k_max = 3;
    constexpr int k_threads = 12;
    constexpr int k_loops = 50;

    csql::manager mgr;
    ASSERT_TRUE(mgr.init(_path, k_max));

    std::atomic<int> in_use{0};
    std::atomic<int> peak{0};
    std::atomic<int> null_count{0};

    std::vector<std::thread> workers;
    workers.reserve(k_threads);
    for (int t = 0; t < k_threads; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < k_loops; ++i) {
                auto cli = mgr.get_client();
                if (!cli) {
                    ++null_count;
                    continue;
                }
                const int now = ++in_use;
                int prev = peak.load();
                while (now > prev && !peak.compare_exchange_weak(prev, now)) {
                }

                (void)cli->exec("SELECT 1;");
                std::this_thread::yield();

                --in_use; // release 전에 감소 → in_use 는 실제 대여 수 이하
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }

    EXPECT_EQ(null_count.load(), 0);
    EXPECT_LE(peak.load(), static_cast<int>(k_max));
    EXPECT_GE(peak.load(), 1);
}

// ───────────── 소멸자 ─────────────

TEST_F(ManagerTest, DestructorWithoutInitDoesNotHang) {
    auto fut = std::async(std::launch::async, [] { csql::manager mgr; });
    EXPECT_EQ(fut.wait_for(k_ready_limit), std::future_status::ready);
}

TEST_F(ManagerTest, DestructorAfterFailedInitDoesNotHang) {
    auto fut = std::async(std::launch::async, [this] {
        csql::manager mgr;
        (void)mgr.init(_path, 0);
    });
    EXPECT_EQ(fut.wait_for(k_ready_limit), std::future_status::ready);
}

TEST_F(ManagerTest, DestructorWaitsForOutstandingClient) {
    auto mgr = std::make_unique<csql::manager>();
    ASSERT_TRUE(mgr->init(_path, 2));

    auto held = mgr->get_client();
    ASSERT_NE(held, nullptr);

    // unique_ptr::reset 은 포인터를 먼저 비우고 delete 하므로 mgr 재접근 없음
    auto destroy = std::async(std::launch::async, [&] { mgr.reset(); });
    EXPECT_EQ(destroy.wait_for(k_block_check), std::future_status::timeout)
        << "빌려준 커넥션이 남았는데 소멸자가 먼저 끝남";

    held.reset(); // 반납 → 소멸자 진행
    EXPECT_EQ(destroy.wait_for(k_ready_limit), std::future_status::ready);
}

TEST_F(ManagerTest, DestructorWakesWaitingGetClientWithNull) {
    auto mgr = std::make_unique<csql::manager>();
    ASSERT_TRUE(mgr->init(_path, 1));
    auto* raw = mgr.get();

    auto held = raw->get_client();
    ASSERT_NE(held, nullptr);

    // 1) 풀이 비어서 대기하는 스레드
    auto waiter = std::async(std::launch::async, [raw] { return raw->get_client(); });
    ASSERT_EQ(waiter.wait_for(k_block_check), std::future_status::timeout);

    // 2) 소멸 시작 → terminating → 대기자는 nullptr 받고 깨어나야 함
    auto destroy = std::async(std::launch::async, [&] { mgr.reset(); });
    ASSERT_EQ(waiter.wait_for(k_ready_limit), std::future_status::ready);
    EXPECT_EQ(waiter.get(), nullptr);

    // 3) held 가 남아 있으니 소멸자는 아직 대기
    EXPECT_EQ(destroy.wait_for(k_block_check), std::future_status::timeout);

    // 4) 반납 → 소멸 완료. (held 반납 전엔 manager 가 살아있으므로 waiter 접근 안전)
    held.reset();
    EXPECT_EQ(destroy.wait_for(k_ready_limit), std::future_status::ready);
}
