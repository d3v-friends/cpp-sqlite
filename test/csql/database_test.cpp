// csql::database 테스트
// - manager 없이 ":memory:" 커넥션을 직접 만들어서 database 로 감싼다.
// - on_release 에서 sqlite3_close(v2 아님)를 불러서, 정리 안 된 stmt 가 남으면 rc 가
//   SQLITE_BUSY(5) 가 되도록 한다. 커넥션 반납 시점에 stmt 누수가 있는지 이걸로 본다.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "csql/cursor.hpp"
#include "csql/database.hpp"
#include "csql/sqlite_row.hpp"
#include "csql/types.hpp"

namespace {
    using ::testing::HasSubstr;

    // 실패 result 의 메시지를 꺼낸다 (한 번만 부를 것)
    std::string message_of(ctls::result_base& res) {
        if (res) {
            return "<success>";
        }
        return res.extract_failed().message;
    }

    // on_release 가 불린 횟수와 close 결과
    struct release_probe {
        int count = 0;
        int close_rc = -1;
        sqlite3* released = nullptr;
    };

    sqlite3* open_memory() {
        sqlite3* raw = nullptr;
        sqlite3_open(":memory:", &raw);
        return raw;
    }

    std::function<void(sqlite3*)> make_release(const std::shared_ptr<release_probe>& probe) {
        return [probe](sqlite3* db) {
            probe->released = db;
            probe->close_rc = sqlite3_close(db);
            ++probe->count;
        };
    }

    // 정리 안 된 stmt 수
    int live_stmts(sqlite3* db) {
        int n = 0;
        for (sqlite3_stmt* s = sqlite3_next_stmt(db, nullptr); s != nullptr;
             s = sqlite3_next_stmt(db, s)) {
            ++n;
        }
        return n;
    }

    // 첫 행의 v 컬럼 정수
    ctls::result<csql::sqlite_int64>
    first_int(const std::shared_ptr<csql::database>& db, const std::string& sql) {
        auto cur = db->query(sql);
        if (!cur) {
            return cur.extract_failed();
        }
        auto row = cur->next();
        if (!row) {
            return row.extract_failed();
        }
        return row->get<csql::sqlite_int64>("v");
    }

    class DatabaseTest : public ::testing::Test {
    protected:
        std::shared_ptr<release_probe> probe = std::make_shared<release_probe>();
        sqlite3* raw = nullptr;
        std::shared_ptr<csql::database> db;

        void SetUp() override {
            raw = open_memory();
            ASSERT_NE(raw, nullptr);
            db = std::make_shared<csql::database>(raw, make_release(probe));
        }

        void TearDown() override {
            // 테스트가 db 를 이미 놓았을 수도 있다
            db.reset();
            EXPECT_EQ(probe->count, 1);
            EXPECT_EQ(probe->close_rc, SQLITE_OK);
        }

        void exec_ok(const std::string& sql) {
            auto res = db->exec(sql);
            ASSERT_TRUE(res) << message_of(res);
        }
    };

    // ---------------------------------------------------------------- 타입 성질
    static_assert(!std::is_copy_constructible_v<csql::database>);
    static_assert(!std::is_copy_assignable_v<csql::database>);
    static_assert(!std::is_move_constructible_v<csql::database>);
    static_assert(!std::is_move_assignable_v<csql::database>);

    // ---------------------------------------------------------------- 생성 / 소멸
    TEST_F(DatabaseTest, GetSqlite3ReturnsWrappedHandle) {
        EXPECT_EQ(db->get_sqlite3(), raw);
    }

    TEST_F(DatabaseTest, DestructorReleasesConnectionOnce) {
        db.reset();
        EXPECT_EQ(probe->count, 1);
        EXPECT_EQ(probe->released, raw);
        EXPECT_EQ(probe->close_rc, SQLITE_OK);
    }

    TEST_F(DatabaseTest, IsSameMutex) {
        std::unique_lock own{db->get_mutex(), std::defer_lock};
        EXPECT_TRUE(db->is_same_mutex(own));

        std::mutex other;
        std::unique_lock foreign{other};
        auto res = db->is_same_mutex(foreign);
        EXPECT_FALSE(res);

        std::unique_lock<std::mutex> empty;
        EXPECT_FALSE(db->is_same_mutex(empty));
    }

    // ---------------------------------------------------------------- query: 정상 경로
    TEST_F(DatabaseTest, QueryBindsArguments) {
        auto v = first_int(db, "SELECT 1 + 2 AS v");
        ASSERT_TRUE(v) << message_of(v);
        EXPECT_EQ(*v, 3);

        auto cur = db->query("SELECT ? + ? AS v", 1, 2);
        ASSERT_TRUE(cur) << message_of(cur);
        auto row = cur->next();
        ASSERT_TRUE(row) << message_of(row);
        auto got = row->get<csql::sqlite_int64>("v");
        ASSERT_TRUE(got) << message_of(got);
        EXPECT_EQ(*got, 3);
    }

    TEST_F(DatabaseTest, QuerySameNumberedParameterCountsOnce) {
        auto cur = db->query("SELECT ?1 || ?1 AS v", "ab");
        ASSERT_TRUE(cur) << message_of(cur);
        auto row = cur->next();
        ASSERT_TRUE(row) << message_of(row);
        auto got = row->get<csql::sqlite_text>("v");
        ASSERT_TRUE(got) << message_of(got);
        EXPECT_EQ(*got, "abab");
    }

    TEST_F(DatabaseTest, QueryNamedParameter) {
        auto cur = db->query("SELECT :a AS v", 9);
        ASSERT_TRUE(cur) << message_of(cur);
        auto row = cur->next();
        ASSERT_TRUE(row) << message_of(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>("v"), 9);
    }

    TEST_F(DatabaseTest, TrailingCommentAndSemicolonsAllowed) {
        for (const char* sql :
             {"SELECT 1 AS v;", "SELECT 1 AS v; -- c", "SELECT 1 AS v; ; /* c */ ;",
              "SELECT 1 AS v;\n\t  "}) {
            auto v = first_int(db, sql);
            ASSERT_TRUE(v) << sql << " : " << message_of(v);
            EXPECT_EQ(*v, 1) << sql;
        }
    }

    // ---------------------------------------------------------------- query: 실패 경로
    TEST_F(DatabaseTest, EmptySqlFails) {
        auto cur = db->query("");
        ASSERT_FALSE(cur);
        EXPECT_THAT(message_of(cur), HasSubstr("sql is empty"));
    }

    TEST_F(DatabaseTest, NoStatementFails) {
        for (const char* sql : {"   ", "-- only comment", "/* c */", ";"}) {
            auto cur = db->query(sql);
            ASSERT_FALSE(cur) << sql;
            EXPECT_THAT(message_of(cur), HasSubstr("sql has no statement")) << sql;
        }
    }

    TEST_F(DatabaseTest, SyntaxErrorKeepsSqliteMessage) {
        auto cur = db->query("SELEC 1");
        ASSERT_FALSE(cur);
        const auto msg = message_of(cur);
        EXPECT_THAT(msg, HasSubstr("failed to prepare sql"));
        EXPECT_THAT(msg, HasSubstr("syntax error"));
    }

    TEST_F(DatabaseTest, MultipleStatementsRejected) {
        for (const char* sql :
             {"SELECT 1; SELECT 2", "SELECT 1; -- c\nSELECT 2", "SELECT 1; SELEC"}) {
            auto cur = db->query(sql);
            ASSERT_FALSE(cur) << sql;
            EXPECT_THAT(message_of(cur), HasSubstr("multiple statements")) << sql;
        }
    }

    TEST_F(DatabaseTest, SqlWithNulRejected) {
        const std::string sql{"SELECT 1;\0 SELECT 2", 19};
        ASSERT_EQ(sql.size(), 19U);
        auto cur = db->query(sql);
        ASSERT_FALSE(cur);
        EXPECT_THAT(message_of(cur), HasSubstr("sql contains NUL"));
    }

    TEST_F(DatabaseTest, BindCountMismatchFails) {
        {
            auto cur = db->query("SELECT ?, ?", 1);
            ASSERT_FALSE(cur);
            EXPECT_THAT(message_of(cur), HasSubstr("bind count mismatch"));
        }
        {
            auto cur = db->query("SELECT ?", 1, 2);
            ASSERT_FALSE(cur);
            EXPECT_THAT(message_of(cur), HasSubstr("bind count mismatch"));
        }
        {
            auto cur = db->query("SELECT 1", 1);
            ASSERT_FALSE(cur);
            EXPECT_THAT(message_of(cur), HasSubstr("bind count mismatch"));
        }
    }

    TEST_F(DatabaseTest, BindFailureReportsIndexAndError) {
        // 값 길이 제한을 줄여서 두 번째 인자 바인딩을 실패시킨다
        sqlite3_limit(raw, SQLITE_LIMIT_LENGTH, 50);
        auto cur = db->query("SELECT ?, ?", 1, std::string(100, 'x'));
        ASSERT_FALSE(cur);
        const auto msg = message_of(cur);
        EXPECT_THAT(msg, HasSubstr("failed to bind"));
        EXPECT_THAT(msg, HasSubstr("index=2"));
        EXPECT_THAT(msg, HasSubstr("too big"));
    }

    TEST_F(DatabaseTest, FailurePathsLeaveNoStatement) {
        sqlite3_limit(raw, SQLITE_LIMIT_LENGTH, 50);
        const std::string nul_sql{"SELECT 1;\0", 10};
        (void)db->query("");
        (void)db->query("  ");
        (void)db->query("SELEC 1");
        (void)db->query("SELECT 1; SELECT 2");
        (void)db->query(nul_sql);
        (void)db->query("SELECT ?, ?", 1);
        (void)db->query("SELECT ?", std::string(100, 'x'));
        EXPECT_EQ(live_stmts(raw), 0);
    }

    TEST_F(DatabaseTest, NotOwnedBySharedPtrFailsWithoutLeak) {
        auto local_probe = std::make_shared<release_probe>();
        sqlite3* local_raw = open_memory();
        {
            csql::database local{local_raw, make_release(local_probe)};
            auto cur = local.query("SELECT 1 AS v");
            ASSERT_FALSE(cur);
            EXPECT_THAT(message_of(cur), HasSubstr("not owned by shared_ptr"));
            EXPECT_EQ(live_stmts(local_raw), 0);
        }
        EXPECT_EQ(local_probe->count, 1);
        EXPECT_EQ(local_probe->close_rc, SQLITE_OK);
    }

    // ---------------------------------------------------------------- query: lock
    TEST_F(DatabaseTest, QueryWithForeignLockFails) {
        std::mutex other;
        std::unique_lock lock{other};
        auto cur = db->query(lock, "SELECT 1");
        ASSERT_FALSE(cur);
        EXPECT_THAT(message_of(cur), HasSubstr("mutex is not same"));
    }

    TEST_F(DatabaseTest, QueryWithEmptyLockFails) {
        std::unique_lock<std::mutex> lock;
        auto cur = db->query(lock, "SELECT 1");
        ASSERT_FALSE(cur);
        EXPECT_THAT(message_of(cur), HasSubstr("mutex is not same"));
    }

    TEST_F(DatabaseTest, QueryWithDeferredLockLocksAndKeepsIt) {
        std::unique_lock lock{db->get_mutex(), std::defer_lock};
        auto cur = db->query(lock, "SELECT 5 AS v");
        ASSERT_TRUE(cur) << message_of(cur);
        EXPECT_TRUE(lock.owns_lock());

        auto row = cur->next(lock);
        ASSERT_TRUE(row) << message_of(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>("v"), 5);
        EXPECT_TRUE(lock.owns_lock());
    }

    TEST_F(DatabaseTest, QueryWithoutLockReleasesMutex) {
        auto cur = db->query("SELECT 1 AS v");
        ASSERT_TRUE(cur) << message_of(cur);
        // query(sql) 이 끝나면 mutex 는 풀려 있어야 한다
        ASSERT_TRUE(db->get_mutex().try_lock());
        db->get_mutex().unlock();
    }

    // ---------------------------------------------------------------- 바인딩 타입 왕복
    TEST_F(DatabaseTest, BindTypesRoundTrip) {
        exec_ok("CREATE TABLE t(k TEXT PRIMARY KEY, v)");

        const std::vector<std::uint8_t> blob{1, 2, 3};
        const char* null_cstr = nullptr;
        const std::optional<std::int64_t> none;
        const std::optional<std::int64_t> some = 11;

        exec_ok("DELETE FROM t");
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('i', ?)", INT64_MAX));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('i_min', ?)", INT64_MIN));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('b', ?)", true));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('d', ?)", 3.5));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('s', ?)", std::string{"hi"}));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('lit', ?)", "lit"));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('empty', ?)", std::string_view{}));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('nul', ?)", std::string{"a\0b", 3}));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('blob', ?)", blob));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('eblob', ?)", std::vector<std::uint8_t>{}));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('n', ?)", nullptr));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('ncstr', ?)", null_cstr));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('none', ?)", none));
        ASSERT_TRUE(db->exec("INSERT INTO t VALUES('some', ?)", some));

        const auto get_row = [&](const char* key) {
            auto cur = db->query("SELECT v, typeof(v) AS ty FROM t WHERE k = ?", key);
            if (!cur) {
                ADD_FAILURE() << key << " : " << message_of(cur);
                return csql::sqlite_row{};
            }
            auto row = cur->next();
            if (!row) {
                ADD_FAILURE() << key << " : " << message_of(row);
                return csql::sqlite_row{};
            }
            return std::move(*row);
        };
        // 실패하면 테스트 실패로 남기고 기본값을 돌려준다 (실패 result 역참조 방지)
        const auto get_v = [&]<class T>(const char* key, const char* col = "v") -> T {
            auto res = get_row(key).get<T>(col);
            if (!res) {
                ADD_FAILURE() << key << "." << col << " : " << message_of(res);
                return T{};
            }
            return *std::move(res);
        };
        const auto type_of = [&](const char* key) {
            return get_v.operator()<csql::sqlite_text>(key, "ty");
        };
        const auto i64 = [&](const char* key) {
            return get_v.operator()<csql::sqlite_int64>(key);
        };
        const auto text = [&](const char* key) {
            return get_v.operator()<csql::sqlite_text>(key);
        };

        EXPECT_EQ(i64("i"), INT64_MAX);
        EXPECT_EQ(i64("i_min"), INT64_MIN);
        EXPECT_EQ(i64("b"), 1);
        EXPECT_EQ(type_of("d"), "real");
        EXPECT_DOUBLE_EQ(get_v.operator()<csql::sqlite_double>("d"), 3.5);
        EXPECT_EQ(text("s"), "hi");
        EXPECT_EQ(text("lit"), "lit");
        EXPECT_EQ(type_of("empty"), "text"); // 빈 string_view 는 NULL 이 아니라 ''
        EXPECT_EQ(text("empty"), "");
        EXPECT_EQ(text("nul"), (std::string{"a\0b", 3}));
        EXPECT_EQ(get_v.operator()<csql::sqlite_blob>("blob"), blob);
        EXPECT_EQ(type_of("eblob"), "blob"); // 빈 blob 도 NULL 이 아니다
        EXPECT_TRUE(get_v.operator()<csql::sqlite_blob>("eblob").empty());
        for (const char* key : {"n", "ncstr", "none"}) {
            EXPECT_EQ(type_of(key), "null") << key;
            EXPECT_TRUE(get_row(key).get<csql::sqlite_null>("v")) << key;
        }
        EXPECT_EQ(i64("some"), 11);
    }

    // ---------------------------------------------------------------- exec
    TEST_F(DatabaseTest, ExecStoresBoundValue) {
        exec_ok("CREATE TABLE t(v INTEGER NOT NULL)");
        auto res = db->exec("INSERT INTO t VALUES(?)", 7);
        ASSERT_TRUE(res) << message_of(res);
        auto v = first_int(db, "SELECT v FROM t");
        ASSERT_TRUE(v) << message_of(v);
        EXPECT_EQ(*v, 7);
    }

    TEST_F(DatabaseTest, ExecRunsStatementOnce) {
        exec_ok("CREATE TABLE t(v)");
        exec_ok("INSERT INTO t VALUES(1)");
        auto v = first_int(db, "SELECT count(*) AS v FROM t");
        ASSERT_TRUE(v);
        EXPECT_EQ(*v, 1);
    }

    TEST_F(DatabaseTest, ExecPropagatesQueryError) {
        auto res = db->exec("INSERT INTO missing VALUES(1)");
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("no such table"));
    }

    TEST_F(DatabaseTest, ExecPropagatesStepError) {
        exec_ok("CREATE TABLE t(v INTEGER NOT NULL)");
        auto res = db->exec("INSERT INTO t VALUES(?)", nullptr);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("NOT NULL"));
        EXPECT_EQ(live_stmts(raw), 0);
    }

    TEST_F(DatabaseTest, ExecWithForeignLockFails) {
        std::mutex other;
        std::unique_lock lock{other};
        auto res = db->exec(lock, "SELECT 1");
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("mutex is not same"));
    }

    TEST_F(DatabaseTest, ExecWithOwnLockKeepsLock) {
        exec_ok("CREATE TABLE t(v)");
        std::unique_lock lock{db->get_mutex()};
        auto res = db->exec(lock, "INSERT INTO t VALUES(?)", 3);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_TRUE(lock.owns_lock());
        auto cur = db->query(lock, "SELECT v FROM t");
        ASSERT_TRUE(cur) << message_of(cur);
        auto row = cur->next(lock);
        ASSERT_TRUE(row) << message_of(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>("v"), 3);
    }

    TEST_F(DatabaseTest, ExecWithoutLockReleasesMutex) {
        exec_ok("CREATE TABLE t(v)");
        ASSERT_TRUE(db->get_mutex().try_lock());
        db->get_mutex().unlock();
    }

    // ---------------------------------------------------------------- 수명 / 동시성
    TEST_F(DatabaseTest, CursorKeepsDatabaseAlive) {
        auto cur = db->query("SELECT 1 AS v UNION ALL SELECT 2");
        ASSERT_TRUE(cur) << message_of(cur);
        db.reset();
        EXPECT_EQ(probe->count, 0); // cursor 가 아직 참조 중

        auto row = cur->next();
        ASSERT_TRUE(row) << message_of(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>("v"), 1);

        // cursor 가 마지막 참조. 소멸하면 stmt 가 먼저 finalize 되고 반납된다
        { auto drop = std::move(cur); }
        EXPECT_EQ(probe->count, 1);
        EXPECT_EQ(probe->close_rc, SQLITE_OK);
    }

    TEST_F(DatabaseTest, ConcurrentExecOnSameConnection) {
        exec_ok("CREATE TABLE t(v INTEGER)");
        constexpr int threads = 4;
        constexpr int per_thread = 50;
        std::atomic<int> failures{0};
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                for (int i = 0; i < per_thread; ++i) {
                    if (!db->exec("INSERT INTO t VALUES(?)", t * per_thread + i)) {
                        ++failures;
                    }
                }
            });
        }
        for (auto& th : pool) {
            th.join();
        }
        EXPECT_EQ(failures.load(), 0);
        auto v = first_int(db, "SELECT count(DISTINCT v) AS v FROM t");
        ASSERT_TRUE(v) << message_of(v);
        EXPECT_EQ(*v, threads * per_thread);
    }
} // namespace
