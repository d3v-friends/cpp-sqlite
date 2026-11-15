// csql::cursor 테스트
// - manager 없이 ":memory:" 커넥션을 database 로 감싸서 쓴다.
// - on_release 에서 sqlite3_close(v2 아님)를 불러서, stmt 가 남은 채 반납되면 rc 가
//   SQLITE_BUSY(5) 로 남게 한다.
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
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

    std::string message_of(ctls::result_base& res) {
        if (res) {
            return "<success>";
        }
        return res.extract_failed().message;
    }

    struct release_probe {
        int count = 0;
        int close_rc = -1;
    };

    std::shared_ptr<csql::database> open_memory(const std::shared_ptr<release_probe>& probe) {
        sqlite3* raw = nullptr;
        sqlite3_open(":memory:", &raw);
        return std::make_shared<csql::database>(raw, [probe](sqlite3* db) {
            probe->close_rc = sqlite3_close(db);
            ++probe->count;
        });
    }

    class CursorTest : public ::testing::Test {
    protected:
        std::shared_ptr<release_probe> probe = std::make_shared<release_probe>();
        std::shared_ptr<csql::database> db;

        void SetUp() override {
            db = open_memory(probe);
        }

        void TearDown() override {
            db.reset();
            EXPECT_EQ(probe->count, 1);
            EXPECT_EQ(probe->close_rc, SQLITE_OK);
        }

        void exec_ok(const std::string& sql) {
            auto res = db->exec(sql);
            ASSERT_TRUE(res) << message_of(res);
        }

        csql::cursor query_ok(const std::string& sql) {
            auto cur = db->query(sql);
            if (!cur) {
                ADD_FAILURE() << sql << " : " << message_of(cur);
                return csql::cursor{nullptr, nullptr};
            }
            return std::move(*cur);
        }

        std::int64_t count(const std::string& table) {
            auto cur = db->query("SELECT count(*) AS n FROM " + table);
            if (!cur) {
                return -1;
            }
            auto row = cur->next();
            if (!row) {
                return -1;
            }
            auto n = row->get<csql::sqlite_int64>("n");
            return n ? *n : -1;
        }
    };

    // ---------------------------------------------------------------- 타입 성질
    static_assert(!std::is_copy_constructible_v<csql::cursor>);
    static_assert(!std::is_copy_assignable_v<csql::cursor>);
    static_assert(std::is_nothrow_move_constructible_v<csql::cursor>);
    // 기본 이동 대입은 _conn 을 _stmt 보다 먼저 바꿔서 막아 두었다
    static_assert(!std::is_move_assignable_v<csql::cursor>);

    // ---------------------------------------------------------------- 순회
    TEST_F(CursorTest, IteratesRowsThenEmpty) {
        auto cur = query_ok("SELECT 1 AS v UNION ALL SELECT 2");
        EXPECT_FALSE(cur.is_end());

        for (const std::int64_t expected : {1, 2}) {
            auto row = cur.next();
            ASSERT_TRUE(row) << message_of(row);
            ASSERT_FALSE(row->is_empty());
            EXPECT_EQ(*row->get<csql::sqlite_int64>("v"), expected);
        }

        // DONE 이후로는 계속 빈 row
        for (int i = 0; i < 2; ++i) {
            auto row = cur.next();
            ASSERT_TRUE(row) << message_of(row);
            EXPECT_TRUE(row->is_empty());
            EXPECT_TRUE(cur.is_end());
        }
    }

    TEST_F(CursorTest, EmptyResultEndsImmediately) {
        exec_ok("CREATE TABLE t(v)");
        auto cur = query_ok("SELECT v FROM t");
        auto row = cur.next();
        ASSERT_TRUE(row) << message_of(row);
        EXPECT_TRUE(row->is_empty());
        EXPECT_TRUE(cur.is_end());
    }

    TEST_F(CursorTest, DoneStatementIsNotReExecuted) {
        exec_ok("CREATE TABLE t(v)");
        auto cur = query_ok("INSERT INTO t VALUES(1)");
        for (int i = 0; i < 3; ++i) {
            auto row = cur.next();
            ASSERT_TRUE(row) << message_of(row);
            EXPECT_TRUE(row->is_empty());
        }
        // SQLite 는 DONE 뒤에 step 하면 자동 reset 후 다시 실행하므로, 1 이어야 한다
        EXPECT_EQ(count("t"), 1);
    }

    // ---------------------------------------------------------------- 실패
    TEST_F(CursorTest, StepFailureIsSticky) {
        // abs(INT64_MIN) 은 step 단계에서 integer overflow
        auto cur = query_ok("SELECT abs(-9223372036854775807 - 1) AS v");
        auto first = cur.next();
        ASSERT_FALSE(first);
        const auto msg1 = message_of(first);
        EXPECT_THAT(msg1, HasSubstr("failed step stmt"));
        EXPECT_THAT(msg1, HasSubstr("integer overflow"));
        EXPECT_TRUE(cur.is_end());

        // 다시 불러도 빈 row(정상 종료처럼 보이는 것)가 아니라 같은 실패
        auto second = cur.next();
        ASSERT_FALSE(second);
        EXPECT_EQ(message_of(second), msg1);
    }

    TEST_F(CursorTest, DuplicateColumnNameFails) {
        auto cur = query_ok("SELECT 1 AS a, 2 AS a");
        auto first = cur.next();
        ASSERT_FALSE(first);
        EXPECT_THAT(message_of(first), HasSubstr("duplicate column name: a"));

        auto second = cur.next();
        ASSERT_FALSE(second);
        EXPECT_THAT(message_of(second), HasSubstr("duplicate column name: a"));
    }

    TEST_F(CursorTest, ColumnNameWithBracesInErrorIsNotFormatted) {
        auto cur = query_ok(R"(SELECT 1 AS "{x}", 2 AS "{x}")");
        auto row = cur.next();
        ASSERT_FALSE(row);
        EXPECT_THAT(message_of(row), HasSubstr("duplicate column name: {x}"));
    }

    // ---------------------------------------------------------------- 이동
    TEST_F(CursorTest, MoveConstructedCursorContinues) {
        auto a = query_ok("SELECT 1 AS v UNION ALL SELECT 2");
        auto r1 = a.next();
        ASSERT_TRUE(r1) << message_of(r1);
        EXPECT_EQ(*r1->get<csql::sqlite_int64>("v"), 1);

        csql::cursor b{std::move(a)};
        auto r2 = b.next();
        ASSERT_TRUE(r2) << message_of(r2);
        EXPECT_EQ(*r2->get<csql::sqlite_int64>("v"), 2);
    }

    TEST_F(CursorTest, MovedFromCursorFailsInsteadOfCrash) {
        auto a = query_ok("SELECT 1 AS v");
        csql::cursor b{std::move(a)};

        // NOLINTNEXTLINE(bugprone-use-after-move): 이동된 뒤 동작 확인이 목적
        auto r1 = a.next();
        ASSERT_FALSE(r1);
        EXPECT_THAT(message_of(r1), HasSubstr("cursor is nullptr"));

        std::unique_lock lock{db->get_mutex(), std::defer_lock};
        auto r2 = a.next(lock); // NOLINT(bugprone-use-after-move)
        ASSERT_FALSE(r2);
        EXPECT_THAT(message_of(r2), HasSubstr("cursor is nullptr"));
        EXPECT_FALSE(lock.owns_lock());

        // 원본이 망가져도 새 cursor 는 정상
        auto r3 = b.next();
        ASSERT_TRUE(r3) << message_of(r3);
        EXPECT_EQ(*r3->get<csql::sqlite_int64>("v"), 1);
    }

    TEST_F(CursorTest, MovedFromFailedCursorStillReportsNullptr) {
        auto a = query_ok("SELECT 1 AS a, 2 AS a");
        ASSERT_FALSE(a.next());
        csql::cursor b{std::move(a)};

        std::unique_lock lock{db->get_mutex(), std::defer_lock};
        auto r = a.next(lock); // NOLINT(bugprone-use-after-move)
        ASSERT_FALSE(r);
        EXPECT_THAT(message_of(r), HasSubstr("cursor is nullptr"));

        // 이동된 쪽은 실패 상태를 그대로 가져간다
        auto rb = b.next();
        ASSERT_FALSE(rb);
        EXPECT_THAT(message_of(rb), HasSubstr("duplicate column name: a"));
    }

    TEST_F(CursorTest, NullConnectionFails) {
        csql::cursor cur{nullptr, nullptr};
        auto r = cur.next();
        ASSERT_FALSE(r);
        EXPECT_THAT(message_of(r), HasSubstr("cursor is nullptr"));
    }

    // ---------------------------------------------------------------- lock
    TEST_F(CursorTest, NextWithForeignLockFails) {
        auto cur = query_ok("SELECT 1 AS v");
        std::mutex other;
        std::unique_lock lock{other};
        auto r = cur.next(lock);
        ASSERT_FALSE(r);
        EXPECT_FALSE(cur.is_end());

        // 실패해도 stmt 는 건드리지 않았으므로 올바른 lock 으로 계속 읽을 수 있다
        auto ok = cur.next();
        ASSERT_TRUE(ok) << message_of(ok);
        EXPECT_EQ(*ok->get<csql::sqlite_int64>("v"), 1);
    }

    TEST_F(CursorTest, NextWithEmptyLockFails) {
        auto cur = query_ok("SELECT 1 AS v");
        std::unique_lock<std::mutex> lock;
        EXPECT_FALSE(cur.next(lock));
    }

    TEST_F(CursorTest, NextWithDeferredLockLocksAndKeepsIt) {
        auto cur = query_ok("SELECT 1 AS v");
        std::unique_lock lock{db->get_mutex(), std::defer_lock};
        auto r = cur.next(lock);
        ASSERT_TRUE(r) << message_of(r);
        EXPECT_EQ(*r->get<csql::sqlite_int64>("v"), 1);
        EXPECT_TRUE(lock.owns_lock());
    }

    TEST_F(CursorTest, NextWithoutLockReleasesMutexBetweenRows) {
        auto cur = query_ok("SELECT 1 AS v UNION ALL SELECT 2");
        ASSERT_TRUE(cur.next());
        ASSERT_TRUE(db->get_mutex().try_lock());
        db->get_mutex().unlock();
        ASSERT_TRUE(cur.next());
    }

    // ---------------------------------------------------------------- 컬럼 타입
    TEST_F(CursorTest, ReadsEveryColumnType) {
        auto cur = query_ok(
            "SELECT 7 AS i, 1.5 AS d, 'hi' AS s, X'010203' AS b, NULL AS n, '' AS es, "
            "zeroblob(0) AS eb, CAST(X'610062' AS TEXT) AS nul");
        auto row = cur.next();
        ASSERT_TRUE(row) << message_of(row);

        EXPECT_EQ(*row->get<csql::sqlite_int64>("i"), 7);
        EXPECT_DOUBLE_EQ(*row->get<csql::sqlite_double>("d"), 1.5);
        EXPECT_EQ(*row->get<csql::sqlite_text>("s"), "hi");
        EXPECT_EQ(*row->get<csql::sqlite_blob>("b"), (csql::sqlite_blob{1, 2, 3}));
        EXPECT_TRUE(row->get<csql::sqlite_null>("n"));

        // 빈 문자열은 NULL 이 아니라 길이 0 text
        auto es = row->get<csql::sqlite_text>("es");
        ASSERT_TRUE(es) << message_of(es);
        EXPECT_EQ(*es, "");
        EXPECT_FALSE(row->get<csql::sqlite_null>("es"));

        // 길이 0 blob 도 NULL 이 아니다
        auto eb = row->get<csql::sqlite_blob>("eb");
        ASSERT_TRUE(eb) << message_of(eb);
        EXPECT_TRUE(eb->empty());

        // NUL 이 들어간 text 는 3바이트 그대로
        EXPECT_EQ(*row->get<csql::sqlite_text>("nul"), (std::string{"a\0b", 3}));
    }

    TEST_F(CursorTest, RowGetErrors) {
        auto cur = query_ok("SELECT 7 AS i, 'x' AS s, NULL AS n");
        auto row = cur.next();
        ASSERT_TRUE(row) << message_of(row);

        // 정수를 double 로 읽으면 변환해 준다
        auto d = row->get<csql::sqlite_double>("i");
        ASSERT_TRUE(d) << message_of(d);
        EXPECT_DOUBLE_EQ(*d, 7.0);

        auto missing = row->get<csql::sqlite_int64>("nope");
        ASSERT_FALSE(missing);
        EXPECT_THAT(message_of(missing), HasSubstr("key not found"));

        auto mismatch = row->get<csql::sqlite_int64>("s");
        ASSERT_FALSE(mismatch);
        EXPECT_THAT(message_of(mismatch), HasSubstr("type mismatch"));

        auto null_int = row->get<csql::sqlite_int64>("n");
        ASSERT_FALSE(null_int);
        EXPECT_THAT(message_of(null_int), HasSubstr("value is null"));

        auto null_text = row->get<csql::sqlite_text>("n");
        ASSERT_FALSE(null_text);

        auto not_null = row->get<csql::sqlite_null>("i");
        ASSERT_FALSE(not_null);
        EXPECT_THAT(message_of(not_null), HasSubstr("type mismatch"));
    }

    // ---------------------------------------------------------------- 수명
    TEST_F(CursorTest, LastOwnerCursorFinalizesBeforeRelease) {
        auto cur = query_ok("SELECT 1 AS v UNION ALL SELECT 2");
        ASSERT_TRUE(cur.next()); // step 중인 stmt 를 남겨 둔다
        db.reset();
        EXPECT_EQ(probe->count, 0);

        { auto drop = std::move(cur); }
        // stmt 가 남아 있었다면 sqlite3_close 가 SQLITE_BUSY 를 돌려준다
        EXPECT_EQ(probe->count, 1);
        EXPECT_EQ(probe->close_rc, SQLITE_OK);
    }

    TEST_F(CursorTest, CursorDestroyedBeforeDoneLeavesNoStatement) {
        sqlite3* raw = db->get_sqlite3();
        {
            auto cur = query_ok("SELECT 1 AS v UNION ALL SELECT 2");
            ASSERT_TRUE(cur.next());
        }
        EXPECT_EQ(sqlite3_next_stmt(raw, nullptr), nullptr);
    }
} // namespace
