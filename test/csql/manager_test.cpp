#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "csql/database.hpp"
#include "csql/manager.hpp"
#include "csql/query.hpp"
#include "csql/sqlite_row.hpp"
#include "test_helper.hpp"

// 멤버 순서 중요: file 이 manager 보다 먼저 선언되어야 manager 가 먼저 소멸한다.
class ManagerTest : public ::testing::Test {
protected:
    csql::test::temp_db_file file;
    csql::manager mgr;
};

TEST_F(ManagerTest, GetBeforeInitializeFails) {
    auto db = mgr.get_database();
    ASSERT_FALSE(db);
    EXPECT_EQ(db.get_message(), std::string{"manager is not ready"});
}

TEST_F(ManagerTest, ZeroConnectionsFails) {
    auto r = mgr.initialize(file.path(), 0);
    ASSERT_FALSE(r);
    EXPECT_FALSE(mgr.get_database());
}

TEST_F(ManagerTest, NegativeBusyTimeoutFails) {
    auto r = mgr.initialize(file.path(), 1, -1);
    ASSERT_FALSE(r);
}

TEST_F(ManagerTest, InitializeTwiceFails) {
    ASSERT_TRUE(mgr.initialize(file.path(), 1));
    auto r = mgr.initialize(file.path(), 1);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.get_message(), std::string{"manager already initialized"});
}

TEST_F(ManagerTest, OpenFailureCleansUpAndDoesNotHang) {
    // 이전 버그: 실패 후 상태가 connecting 으로 남아 소멸자가 영원히 대기
    auto r = mgr.initialize("/csql_no_such_dir/sub/a.db", 3);
    ASSERT_FALSE(r);
    EXPECT_NE(r.get_message().find("failed to open database"), std::string::npos)
        << r.get_message();
    EXPECT_FALSE(mgr.get_database());
    // 테스트가 끝나면서 mgr 소멸자가 바로 돌아와야 한다
}

TEST_F(ManagerTest, PragmasApplied) {
    ASSERT_TRUE(mgr.initialize(file.path(), 1));
    auto db = mgr.get_database();
    ASSERT_TRUE(db);

    auto jm = csql::query(*db, "PRAGMA journal_mode");
    ASSERT_TRUE(jm);
    auto row = (*jm)->next();
    ASSERT_TRUE(row);
    EXPECT_EQ(*row->get<csql::sqlite_text>(0), std::string{"wal"});

    auto fk = csql::query(*db, "PRAGMA foreign_keys");
    ASSERT_TRUE(fk);
    auto fk_row = (*fk)->next();
    ASSERT_TRUE(fk_row);
    EXPECT_EQ(*fk_row->get<csql::sqlite_int64>(0), 1);
}

TEST_F(ManagerTest, ConnectionReturnsToPool) {
    ASSERT_TRUE(mgr.initialize(file.path(), 1));
    for (int i = 0; i < 3; ++i) {
        auto db = mgr.get_database(); // 반납이 안 되면 두 번째에서 멈춘다
        ASSERT_TRUE(db);
    }
}

TEST_F(ManagerTest, WaitsUntilConnectionReleased) {
    ASSERT_TRUE(mgr.initialize(file.path(), 1));
    auto held = mgr.get_database();
    ASSERT_TRUE(held);

    std::atomic<bool> got{false};
    std::thread waiter{[&] {
        auto db = mgr.get_database();
        got = static_cast<bool>(db);
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    EXPECT_FALSE(got.load()); // 풀이 비어서 기다리는 중

    *held = nullptr; // database 반납
    waiter.join();
    EXPECT_TRUE(got.load());
}

TEST_F(ManagerTest, CursorHoldsConnectionUntilDestroyed) {
    ASSERT_TRUE(mgr.initialize(file.path(), 1));
    std::unique_ptr<csql::cursor> cur;
    {
        auto db = mgr.get_database();
        ASSERT_TRUE(db);
        auto q = csql::query(std::move(*db), "SELECT 1");
        ASSERT_TRUE(q);
        cur = std::move(*q);
    }

    std::atomic<bool> got{false};
    std::thread waiter{[&] {
        auto db = mgr.get_database();
        got = static_cast<bool>(db);
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    EXPECT_FALSE(got.load()); // 커서가 커넥션을 들고 있음

    cur.reset();
    waiter.join();
    EXPECT_TRUE(got.load());
}

TEST_F(ManagerTest, ThreadsUseOwnConnectionsWithoutLock) {
    // 락 없는 구조의 정상 사용법: 스레드마다 자기 커넥션을 빌려서 쓴다
    constexpr int threads = 8;
    constexpr int per_thread = 50;
    ASSERT_TRUE(mgr.initialize(file.path(), 4));
    {
        auto db = mgr.get_database();
        ASSERT_TRUE(db);
        ASSERT_TRUE(csql::exec(*db, "CREATE TABLE w(th INTEGER, n INTEGER)"));
    }

    std::atomic<int> failures{0};
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            for (int n = 0; n < per_thread; ++n) {
                auto db = mgr.get_database();
                if (!db) {
                    ++failures;
                    continue;
                }
                if (!csql::exec(*db, "INSERT INTO w(th, n) VALUES (?, ?)", t, n)) {
                    ++failures;
                }
            }
        });
    }
    for (auto& th : pool) {
        th.join();
    }
    EXPECT_EQ(failures.load(), 0);

    auto db = mgr.get_database();
    ASSERT_TRUE(db);
    auto cnt = csql::query(*db, "SELECT count(*) FROM w");
    ASSERT_TRUE(cnt);
    auto row = (*cnt)->next();
    ASSERT_TRUE(row);
    EXPECT_EQ(*row->get<csql::sqlite_int64>(0), threads * per_thread);
}

TEST(ManagerLifetimeTest, DestructorWakesWaitersAndWaitsForRelease) {
    // 소멸이 시작되면 기다리던 get_database 는 실패로 깨어나고,
    // 소멸자는 빌려준 커넥션이 반납될 때까지 기다린다.
    csql::test::temp_db_file file;
    auto mgr = std::make_unique<csql::manager>();
    ASSERT_TRUE(mgr->initialize(file.path(), 1));

    auto held = mgr->get_database();
    ASSERT_TRUE(held);

    std::string waiter_message;
    // unique_ptr 변수 자체는 main 이 reset 하므로 raw 포인터를 넘긴다
    std::thread waiter{[m = mgr.get(), &waiter_message] {
        auto db = m->get_database(); // 풀이 비어서 대기
        if (!db) {
            waiter_message = db.get_message();
        }
    }};

    // 다른 스레드가 커넥션을 늦게 반납한다
    std::atomic<bool> returned{false};
    std::thread holder{[db = std::move(*held), &returned]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        returned = true;
        db.reset();
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    mgr.reset(); // 반납될 때까지 여기서 기다림
    EXPECT_TRUE(returned.load());

    waiter.join();
    holder.join();
    EXPECT_EQ(waiter_message, std::string{"manager is terminating"});
}
