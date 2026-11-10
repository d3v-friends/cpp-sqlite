// connection.hpp 단위 테스트
// - connection 은 sqlite3* 를 저장만 하고 역참조하지 않으므로, 수명/콜백 테스트는 가짜 핸들로,
//   mutex 직렬화/실제 close 테스트는 :memory: DB 로 확인한다.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <sqlite3.h>
#include <gtest/gtest.h>

#include "csql/connection.hpp"

using namespace std::chrono_literals;

namespace {
    // 실제로 열지 않은 가짜 핸들 (connection 은 포인터를 전달만 함)
    sqlite3* fake_db(const std::uintptr_t n) {
        return reinterpret_cast<sqlite3*>(n);
    }

    sqlite3* open_memory_db() {
        sqlite3* db = nullptr;
        if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
            sqlite3_close(db);
            return nullptr;
        }
        return db;
    }

    bool exec(sqlite3* db, const std::string& sql) {
        return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    std::int64_t query_int(sqlite3* db, const char* sql) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
        std::int64_t v = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW) v = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return v;
    }

    // 다른 스레드에서 try_lock (같은 스레드에서 std::mutex 재잠금은 UB 라서)
    bool try_lock_from_other_thread(std::mutex& m) {
        return std::async(std::launch::async, [&m] {
                   if (!m.try_lock()) return false;
                   m.unlock();
                   return true;
               })
            .get();
    }
} // namespace

// ---------------------------------------------------------------------------
// 타입 특성 (컴파일 타임)
// ---------------------------------------------------------------------------
static_assert(!std::is_default_constructible_v<csql::connection>);
static_assert(!std::is_copy_constructible_v<csql::connection>);
static_assert(!std::is_copy_assignable_v<csql::connection>);
static_assert(!std::is_move_constructible_v<csql::connection>);
static_assert(!std::is_move_assignable_v<csql::connection>);
// 소멸자는 암묵적으로 noexcept → on_release 가 던지면 std::terminate (아래 DeathTest 참고)
static_assert(std::is_nothrow_destructible_v<csql::connection>);
static_assert(noexcept(std::declval<const csql::connection&>().get_db()));
static_assert(noexcept(std::declval<const csql::connection&>().get_mutex()));
static_assert(std::is_same_v<decltype(std::declval<const csql::connection&>().get_mutex()), std::mutex&>);

// ---------------------------------------------------------------------------
// get_db
// ---------------------------------------------------------------------------
TEST(ConnectionTest, GetDbReturnsHandle) {
    const csql::connection conn{fake_db(0x1000), [](sqlite3*) {}};
    EXPECT_EQ(conn.get_db(), fake_db(0x1000));
}

TEST(ConnectionTest, GetDbAcceptsNullHandle) {
    sqlite3* released = fake_db(0xdead);
    {
        const csql::connection conn{nullptr, [&](sqlite3* db) { released = db; }};
        EXPECT_EQ(conn.get_db(), nullptr);
    }
    EXPECT_EQ(released, nullptr); // nullptr 도 그대로 콜백에 전달
}

// ---------------------------------------------------------------------------
// 소멸 / on_release
// ---------------------------------------------------------------------------
TEST(ConnectionTest, DestructorCallsReleaseOnceWithSameHandle) {
    int count = 0;
    sqlite3* released = nullptr;
    {
        csql::connection conn{fake_db(0x2000), [&](sqlite3* db) {
                                  ++count;
                                  released = db;
                              }};
        EXPECT_EQ(count, 0); // 살아있는 동안 호출 안 됨
    }
    EXPECT_EQ(count, 1);
    EXPECT_EQ(released, fake_db(0x2000));
}

TEST(ConnectionTest, ReleaseCallbackStateDestroyedWithConnection) {
    auto token = std::make_shared<int>(0);
    std::weak_ptr<int> watch = token;
    {
        csql::connection conn{fake_db(1), [t = std::move(token)](sqlite3*) { ++*t; }};
        EXPECT_FALSE(watch.expired()); // 콜백이 캡처 상태를 들고 있음
    }
    EXPECT_TRUE(watch.expired()); // connection 과 함께 std::function 도 정리됨
}

// ---------------------------------------------------------------------------
// shared_ptr 공유 수명
// ---------------------------------------------------------------------------
TEST(ConnectionSharedTest, ReleasesOnlyAfterLastOwner) {
    int count = 0;
    auto a = std::make_shared<csql::connection>(fake_db(3), [&](sqlite3*) { ++count; });
    auto b = a;
    auto c = a;
    EXPECT_EQ(a.use_count(), 3);

    a.reset();
    EXPECT_EQ(count, 0);
    b.reset();
    EXPECT_EQ(count, 0);
    c.reset();
    EXPECT_EQ(count, 1);
}

TEST(ConnectionSharedTest, MovingOwnerDoesNotRelease) {
    int count = 0;
    auto a = std::make_shared<csql::connection>(fake_db(4), [&](sqlite3*) { ++count; });
    auto b = std::move(a);
    EXPECT_EQ(a, nullptr);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(b->get_db(), fake_db(4));
    b.reset();
    EXPECT_EQ(count, 1);
}

TEST(ConnectionSharedTest, WeakPtrExpiresAfterRelease) {
    int count = 0;
    auto conn = std::make_shared<csql::connection>(fake_db(5), [&](sqlite3*) { ++count; });
    std::weak_ptr<csql::connection> weak = conn;

    ASSERT_NE(weak.lock(), nullptr);
    conn.reset();
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(count, 1);
}

TEST(ConnectionSharedTest, ReleaseRunsOnThreadDroppingLastOwner) {
    std::thread::id released_on{};
    auto conn = std::make_shared<csql::connection>(
        fake_db(6), [&](sqlite3*) { released_on = std::this_thread::get_id(); });

    std::promise<void> go;
    std::thread worker{[c = conn, f = go.get_future()]() mutable {
        f.wait();
        c.reset(); // 마지막 소유자
    }};
    const auto worker_id = worker.get_id();

    conn.reset();                              // 메인 스레드는 먼저 놓음
    EXPECT_EQ(released_on, std::thread::id{}); // 아직 반납 안 됨

    go.set_value();
    worker.join();
    EXPECT_EQ(released_on, worker_id); // 반납 콜백은 마지막으로 놓은 스레드에서 실행
}

// ---------------------------------------------------------------------------
// 실제 sqlite 핸들
// ---------------------------------------------------------------------------
TEST(ConnectionDbTest, ReleaseClosesRealDatabase) {
    sqlite3* db = open_memory_db();
    ASSERT_NE(db, nullptr);

    int close_rc = -1;
    {
        csql::connection conn{db, [&](sqlite3* h) { close_rc = sqlite3_close(h); }};
        ASSERT_TRUE(exec(conn.get_db(), "CREATE TABLE t(v INTEGER); INSERT INTO t VALUES (42);"));
        EXPECT_EQ(query_int(conn.get_db(), "SELECT v FROM t"), 42);
    }
    EXPECT_EQ(close_rc, SQLITE_OK); // 남은 stmt 없이 정상 close
}

TEST(ConnectionDbTest, ReleaseCanReturnHandleToPool) {
    // manager 의 풀 반납 패턴: 콜백이 핸들을 풀에 돌려놓고, 다시 꺼내 새 connection 으로 감쌈
    sqlite3* db = open_memory_db();
    ASSERT_NE(db, nullptr);
    std::vector<sqlite3*> pool{db};
    auto give_back = [&](sqlite3* h) { pool.push_back(h); };

    for (int round = 0; round < 3; ++round) {
        ASSERT_EQ(pool.size(), 1u);
        sqlite3* h = pool.back();
        pool.pop_back();
        {
            auto conn = std::make_shared<csql::connection>(h, give_back);
            EXPECT_TRUE(pool.empty());
            ASSERT_TRUE(exec(conn->get_db(), "CREATE TABLE IF NOT EXISTS t(v INTEGER);"
                                             "INSERT INTO t VALUES (1);"));
        }
        ASSERT_EQ(pool.size(), 1u);
        EXPECT_EQ(pool.back(), db); // 같은 핸들이 돌아옴
    }
    EXPECT_EQ(query_int(db, "SELECT COUNT(*) FROM t"), 3); // 데이터도 유지
    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);
}

// ---------------------------------------------------------------------------
// mutex
// ---------------------------------------------------------------------------
TEST(ConnectionMutexTest, GetMutexReturnsSameObject) {
    csql::connection conn{fake_db(7), [](sqlite3*) {}};
    const csql::connection& cref = conn;
    EXPECT_EQ(&conn.get_mutex(), &conn.get_mutex());
    EXPECT_EQ(&cref.get_mutex(), &conn.get_mutex()); // const 에서도 같은(잠글 수 있는) mutex
}

TEST(ConnectionMutexTest, EachConnectionHasOwnMutex) {
    csql::connection a{fake_db(8), [](sqlite3*) {}};
    csql::connection b{fake_db(9), [](sqlite3*) {}};
    EXPECT_NE(&a.get_mutex(), &b.get_mutex());

    std::scoped_lock lock{a.get_mutex()};
    EXPECT_TRUE(try_lock_from_other_thread(b.get_mutex())); // a 를 잡아도 b 는 자유
}

TEST(ConnectionMutexTest, LockedMutexIsExclusiveAcrossThreads) {
    const csql::connection conn{fake_db(10), [](sqlite3*) {}};
    {
        std::scoped_lock lock{conn.get_mutex()};
        EXPECT_FALSE(try_lock_from_other_thread(conn.get_mutex()));
    }
    EXPECT_TRUE(try_lock_from_other_thread(conn.get_mutex()));
}

TEST(ConnectionMutexTest, WaiterBlocksUntilUnlock) {
    const csql::connection conn{fake_db(11), [](sqlite3*) {}};
    std::unique_lock lock{conn.get_mutex()};

    auto waiter = std::async(std::launch::async, [&conn] {
        std::scoped_lock l{conn.get_mutex()};
        return true;
    });
    EXPECT_EQ(waiter.wait_for(200ms), std::future_status::timeout); // 잡혀 있는 동안 대기

    lock.unlock();
    ASSERT_EQ(waiter.wait_for(2s), std::future_status::ready); // 풀리면 진행
    EXPECT_TRUE(waiter.get());
}

TEST(ConnectionMutexTest, MutexSerializesReadModifyWrite) {
    // SELECT → C++ 에서 +1 → UPDATE 는 원자적이지 않음. mutex 로 직렬화되면 정확히 합산된다.
    sqlite3* db = open_memory_db();
    ASSERT_NE(db, nullptr);
    auto conn = std::make_shared<csql::connection>(db, [](sqlite3* h) { sqlite3_close(h); });
    ASSERT_TRUE(exec(conn->get_db(), "CREATE TABLE c(v INTEGER); INSERT INTO c VALUES (0);"));

    constexpr int kThreads = 8;
    constexpr int kIters = 200;
    std::atomic<int> inside{0};
    std::atomic<int> max_inside{0};
    std::atomic<bool> ok{true};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([conn, &inside, &max_inside, &ok] {
            for (int i = 0; i < kIters; ++i) {
                std::scoped_lock lock{conn->get_mutex()};
                const int now = ++inside;
                int prev = max_inside.load();
                while (prev < now && !max_inside.compare_exchange_weak(prev, now)) {}

                const auto v = query_int(conn->get_db(), "SELECT v FROM c");
                if (v < 0 || !exec(conn->get_db(), "UPDATE c SET v = " + std::to_string(v + 1))) {
                    ok = false;
                }
                --inside;
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_TRUE(ok);
    EXPECT_EQ(max_inside.load(), 1); // 임계구역에 동시에 1개만
    EXPECT_EQ(query_int(conn->get_db(), "SELECT v FROM c"), kThreads * kIters);
}

// ---------------------------------------------------------------------------
// 비정상 콜백
// ---------------------------------------------------------------------------
TEST(ConnectionTest, EmptyReleaseCallbackThrows) {
    // 생성자에서 빈 콜백을 막는다 → invalid_argument
    // 매크로 인자 안의 {a, b} 콤마가 인자 구분자로 잘리지 않도록 람다로 감싼다
    const auto make = [] { csql::connection conn{fake_db(12), std::function<void(sqlite3*)>{}}; };
    EXPECT_THROW(make(), std::invalid_argument);
}

TEST(ConnectionDeathTest, ThrowingReleaseCallbackTerminates) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto scope = [] {
        csql::connection conn{fake_db(13), [](sqlite3*) { throw std::runtime_error("release failed"); }};
    };
    EXPECT_DEATH(scope(), "");
}
