// source/manager_test.cpp
// sql::manager 커넥션 풀 테스트 (gtest)
#include <gtest/gtest.h>

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "client.hpp"
#include "manager.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
    // 테스트마다 다른 임시 DB 파일을 쓰고, 끝나면 -wal / -shm 까지 지운다
    class ManagerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            _path = fs::temp_directory_path() /
                    (std::string("cpp_sqlite_") + info->name() + ".db");
            remove_files();
        }
        void TearDown() override { remove_files(); }

        [[nodiscard]] std::string path() const { return _path.string(); }

        // 풀과 상관없는 별도 연결로 결과 확인
        [[nodiscard]] long long count(const std::string& table) const {
            sqlite3* raw = nullptr;
            sqlite3_open_v2(_path.string().c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
            sql::sqlite3_ptr conn{raw};

            sqlite3_stmt* stmt = nullptr;
            const std::string q = "SELECT COUNT(*) FROM " + table + ";";
            if (sqlite3_prepare_v2(conn.get(), q.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return -1;
            }
            long long n = -1;
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                n = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
            return n;
        }

    private:
        void remove_files() const {
            std::error_code ec;
            for (const char* suffix : {"", "-wal", "-shm"}) {
                fs::remove(_path.string() + suffix, ec);
            }
        }

        fs::path _path;
    };

    // 함수가 timeout 안에 끝나는지 확인한다.
    // 끝나지 않으면(데드락) 스레드를 detach 해서 테스트 프로세스가 멈추지 않게 한다.
    // 이 경우 관련 객체는 일부러 누수시킨다.
    bool finishes_within(std::chrono::milliseconds timeout, std::function<void()> fn) {
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();
        std::thread{[fn = std::move(fn), done] {
            fn();
            done->set_value();
        }}.detach();
        return fut.wait_for(timeout) == std::future_status::ready;
    }
} // namespace

// ───────────── connect ─────────────

TEST_F(ManagerTest, ConnectSucceeds) {
    sql::manager mgr{path(), 4};
    EXPECT_TRUE(mgr.connect());
    EXPECT_TRUE(fs::exists(path()));
}

TEST_F(ManagerTest, ConnectTwiceFails) {
    sql::manager mgr{path(), 2};
    ASSERT_TRUE(mgr.connect());
    EXPECT_FALSE(mgr.connect());
}

TEST_F(ManagerTest, ConnectFailsOnInvalidPath) {
    sql::manager mgr{"/nonexistent_dir_for_test/a/b/test.db", 2};
    EXPECT_FALSE(mgr.connect());
    EXPECT_EQ(mgr.get_client(), nullptr); // terminated 상태
}

TEST_F(ManagerTest, ConnectEnablesWal) {
    {
        sql::manager mgr{path(), 1};
        ASSERT_TRUE(mgr.connect());
    }
    sqlite3* raw = nullptr;
    sqlite3_open(path().c_str(), &raw);
    sql::sqlite3_ptr conn{raw};
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(conn.get(), "PRAGMA journal_mode;", -1, &stmt, nullptr),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_STREQ(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "wal");
    sqlite3_finalize(stmt);
}

// ───────────── get_client ─────────────

TEST_F(ManagerTest, GetClientBeforeConnectReturnsNull) {
    sql::manager mgr{path(), 2};
    EXPECT_EQ(mgr.get_client(), nullptr);
}

TEST_F(ManagerTest, ClientExecWritesData) {
    sql::manager mgr{path(), 2};
    ASSERT_TRUE(mgr.connect());

    auto c = mgr.get_client();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->exec("CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);"));
    EXPECT_TRUE(c->exec("INSERT INTO t(v) VALUES('a'),('b'),('c');"));
    c.reset();

    EXPECT_EQ(count("t"), 3);
}

TEST_F(ManagerTest, ClientExecInvalidSqlReturnsFalse) {
    sql::manager mgr{path(), 1};
    ASSERT_TRUE(mgr.connect());

    auto c = mgr.get_client();
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->exec("SELEC oops;"));
    EXPECT_FALSE(c->exec("INSERT INTO no_such_table VALUES(1);"));
    EXPECT_TRUE(c->exec("SELECT 1;")); // 실패 후에도 연결은 계속 쓸 수 있어야 함
}

TEST_F(ManagerTest, CanBorrowUpToMaxConnections) {
    constexpr std::size_t max = 4;
    sql::manager mgr{path(), max};
    ASSERT_TRUE(mgr.connect());

    std::vector<std::unique_ptr<sql::client>> held;
    for (std::size_t i = 0; i < max; ++i) {
        held.push_back(mgr.get_client());
        ASSERT_NE(held.back(), nullptr) << "i=" << i;
    }
}

TEST_F(ManagerTest, ReleasedClientIsReused) {
    sql::manager mgr{path(), 1};
    ASSERT_TRUE(mgr.connect());

    // 풀이 1개라서 반납이 안 되면 두 번째 get_client 에서 멈춘다
    const bool ok = finishes_within(2s, [&] {
        for (int i = 0; i < 100; ++i) {
            auto c = mgr.get_client();
            ASSERT_NE(c, nullptr);
        }
    });
    ASSERT_TRUE(ok) << "client 반납 후 재사용이 안 됨 (get_client 대기 중)";
}

TEST_F(ManagerTest, GetClientBlocksWhenPoolIsFull) {
    sql::manager mgr{path(), 2};
    ASSERT_TRUE(mgr.connect());

    auto c1 = mgr.get_client();
    auto c2 = mgr.get_client();
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);

    auto waiter = std::async(std::launch::async, [&] { return mgr.get_client(); });

    // 풀이 꽉 찼으니 기다려야 함
    EXPECT_EQ(waiter.wait_for(200ms), std::future_status::timeout);

    // 하나 반납하면 깨어나서 받아가야 함
    c1.reset();
    ASSERT_EQ(waiter.wait_for(2s), std::future_status::ready);
    auto c3 = waiter.get();
    EXPECT_NE(c3, nullptr);
}

// ───────────── 동시성 ─────────────

TEST_F(ManagerTest, ConcurrentInsertsRespectPoolLimit) {
    constexpr std::size_t max = 4;
    constexpr int threads = 16;
    constexpr int per_thread = 100;

    sql::manager mgr{path(), max};
    ASSERT_TRUE(mgr.connect());
    {
        auto c = mgr.get_client();
        ASSERT_NE(c, nullptr);
        ASSERT_TRUE(c->exec("CREATE TABLE t(id INTEGER PRIMARY KEY, th INTEGER, n INTEGER,"
                            " UNIQUE(th, n));"));
    }

    std::atomic<int> in_use{0};
    std::atomic<int> peak{0};
    std::atomic<int> ok{0};
    std::atomic<int> null_client{0};

    const bool finished = finishes_within(60s, [&] {
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                for (int n = 0; n < per_thread; ++n) {
                    auto c = mgr.get_client();
                    if (!c) {
                        ++null_client;
                        continue;
                    }
                    const int now = ++in_use;
                    int prev = peak.load();
                    while (prev < now && !peak.compare_exchange_weak(prev, now)) {
                    }

                    if (c->exec("INSERT INTO t(th, n) VALUES(" + std::to_string(t) + "," +
                                std::to_string(n) + ");")) {
                        ++ok;
                    }
                    --in_use;
                } // c 소멸 → 반납
            });
        }
        for (auto& th : pool) {
            th.join();
        }
    });

    ASSERT_TRUE(finished) << "동시 대여/반납 중 멈춤 (데드락 의심)";
    EXPECT_EQ(null_client.load(), 0);
    EXPECT_EQ(ok.load(), threads * per_thread);
    EXPECT_LE(peak.load(), static_cast<int>(max));
    EXPECT_GE(peak.load(), 2) << "실제로 여러 연결이 동시에 쓰이지 않음";
    EXPECT_EQ(count("t"), threads * per_thread);
}

// ───────────── 종료 ─────────────

TEST_F(ManagerTest, DestructorReturnsWhenIdle) {
    auto* mgr = new sql::manager{path(), 4};
    ASSERT_TRUE(mgr->connect());
    {
        auto c = mgr->get_client();
        ASSERT_NE(c, nullptr);
    }
    EXPECT_TRUE(finishes_within(2s, [mgr] { delete mgr; }));
}

TEST_F(ManagerTest, DestructorWithoutConnectReturns) {
    auto* mgr = new sql::manager{path(), 4};
    EXPECT_TRUE(finishes_within(2s, [mgr] { delete mgr; }));
}

TEST_F(ManagerTest, DestructorWaitsForBorrowedClient) {
    auto* mgr = new sql::manager{path(), 1};
    ASSERT_TRUE(mgr->connect());
    auto c = mgr->get_client();
    ASSERT_NE(c, nullptr);

    std::atomic<bool> destroyed{false};
    auto destroyer = std::async(std::launch::async, [&] {
        delete mgr;
        destroyed = true;
    });

    // client 를 들고 있는 동안은 소멸자가 끝나면 안 됨
    EXPECT_EQ(destroyer.wait_for(200ms), std::future_status::timeout);
    EXPECT_FALSE(destroyed.load());

    c.reset(); // 반납 → 소멸자 진행
    EXPECT_EQ(destroyer.wait_for(2s), std::future_status::ready);
}

// 반납과 소멸이 경쟁할 때 대기 중인 get_client 가 nullptr 를 받고,
// 소멸자도 멈추지 않고 끝나야 한다. 타이밍 문제라 여러 번 반복한다.
TEST_F(ManagerTest, DestructorWakesWaitersWithoutHang) {
    constexpr int rounds = 200;
    constexpr int waiters = 4;

    for (int r = 0; r < rounds; ++r) {
        auto* mgr = new sql::manager{path(), 1};
        ASSERT_TRUE(mgr->connect());
        auto holder = std::make_shared<std::unique_ptr<sql::client>>(mgr->get_client());
        ASSERT_NE(*holder, nullptr);

        const bool ok = finishes_within(3s, [mgr, holder] {
            std::vector<std::thread> ws;
            for (int i = 0; i < waiters; ++i) {
                ws.emplace_back([mgr] {
                    // 반납이 소멸보다 먼저 일어나면 client 를 받을 수도 있음 → 바로 반납
                    auto c = mgr->get_client();
                });
            }
            std::this_thread::sleep_for(5ms); // waiter 들이 wait 에 들어가게

            std::thread destroyer{[mgr] { delete mgr; }};
            std::this_thread::sleep_for(1ms);
            holder->reset(); // 소멸 중 반납 → 경쟁 상황

            for (auto& w : ws) {
                w.join();
            }
            destroyer.join();
        });

        ASSERT_TRUE(ok) << "round " << r << ": 소멸자 또는 대기 스레드가 멈춤";
    }
}
