// [2026-11-09] cursor.hpp 테스트 (gtest)
// 옮길 위치: source/cursor_test.cpp  (cmake/test.cmake 가 source/*_test.cpp 를 수집)
//
// manager / client 를 거치지 않고 cursor 만 따로 검증한다.
//   :memory: DB → csql::connection(shared_ptr) → sqlite3_prepare_v2 → csql::cursor
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "types.hpp"
#include "connection.hpp"
#include "cursor.hpp"
#include "sqlite_row.hpp"

namespace {
    using namespace std::chrono_literals;

    // ───────────────────────── 컴파일 타임 ─────────────────────────

    static_assert(!std::is_copy_constructible_v<csql::cursor>);
    static_assert(!std::is_copy_assignable_v<csql::cursor>);
    static_assert(std::is_nothrow_move_constructible_v<csql::cursor>);
    static_assert(std::is_nothrow_move_assignable_v<csql::cursor>);
    static_assert(!std::is_default_constructible_v<csql::cursor>);
    static_assert(std::is_same_v<decltype(std::declval<csql::cursor&>().next()),
                                 ctls::result<csql::sqlite_row>>);

    // [2026-11-09] sqlite_row: 복사 금지 / move 만 (next() 결과를 꺼낼 때 std::move 필요)
    static_assert(!std::is_copy_constructible_v<csql::sqlite_row>);
    static_assert(!std::is_copy_assignable_v<csql::sqlite_row>);
    static_assert(std::is_nothrow_move_constructible_v<csql::sqlite_row>);

    // 1 ~ n 까지 한 줄씩 나오는 SELECT (테이블 없이)
    std::string seq_sql(const int n) {
        return "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < " +
               std::to_string(n) + ") SELECT x FROM c";
    }

    // ───────────────────────── fixture ─────────────────────────

    class CursorTest : public ::testing::Test {
    protected:
        void SetUp() override {
            sqlite3* raw = nullptr;
            ASSERT_EQ(sqlite3_open(":memory:", &raw), SQLITE_OK);

            // release 콜백에서 sqlite3_close(v2 아님) 를 쓴다.
            // finalize 안 된 stmt 가 남아 있으면 SQLITE_BUSY 가 나오므로
            // "cursor 가 stmt 를 먼저 정리하고 커넥션을 놓는지" 까지 확인할 수 있다.
            auto close_rc = _close_rc;
            auto released = _released;
            _conn = std::make_shared<csql::connection>(raw, [close_rc, released](sqlite3* db) {
                close_rc->store(sqlite3_close(db));
                released->fetch_add(1);
            });
        }

        void TearDown() override {
            // 테스트 본문의 cursor 는 이미 소멸됐으므로 여기서 마지막 소유자가 놓는다
            if (_conn) {
                _conn.reset();
                EXPECT_EQ(_released->load(), 1);
                EXPECT_EQ(_close_rc->load(), SQLITE_OK) << "finalize 안 된 stmt 가 남아 있음";
            }
        }

        void exec(const std::string& sql) const {
            char* err = nullptr;
            const int rc = sqlite3_exec(_conn->get_db(), sql.c_str(), nullptr, nullptr, &err);
            EXPECT_EQ(rc, SQLITE_OK) << (err ? err : "unknown error") << " / sql=" << sql;
            sqlite3_free(err);
        }

        std::unique_ptr<csql::cursor> make_cursor(const std::string& sql) const {
            sqlite3_stmt* stmt = nullptr;
            const int rc = sqlite3_prepare_v2(_conn->get_db(), sql.data(),
                                              static_cast<int>(sql.size()), &stmt, nullptr);
            EXPECT_EQ(rc, SQLITE_OK) << sqlite3_errmsg(_conn->get_db()) << " / sql=" << sql;
            if (rc != SQLITE_OK) return nullptr;
            return std::make_unique<csql::cursor>(_conn, csql::sqlite3_stmt_ptr{stmt});
        }

        // 다음 줄을 읽는다. 실패하면 테스트 실패 + 빈 row
        static csql::sqlite_row next_ok(csql::cursor& cur) {
            auto r = cur.next();
            EXPECT_TRUE(r) << r.get_message();
            if (!r) return csql::sqlite_row{};
            return std::move(*r);
        }

        static std::int64_t count(const CursorTest& self, const std::string& sql) {
            auto cur = self.make_cursor(sql);
            if (!cur) return -1;
            auto row = next_ok(*cur);
            auto v = row.get<csql::sqlite_int64>(0);
            EXPECT_TRUE(v) << v.get_message();
            return v ? *v : -1;
        }

        std::shared_ptr<csql::connection> _conn;
        std::shared_ptr<std::atomic<int>> _close_rc = std::make_shared<std::atomic<int>>(-1);
        std::shared_ptr<std::atomic<int>> _released = std::make_shared<std::atomic<int>>(0);
    };

    // ───────────────────────── 한 줄 / 여러 줄 읽기 ─────────────────────────

    TEST_F(CursorTest, ReadsSingleRowThenEnd) {
        auto cur = make_cursor("SELECT 1");
        ASSERT_NE(cur, nullptr);

        auto row = next_ok(*cur);
        ASSERT_FALSE(row.is_empty());
        auto v = row.get<csql::sqlite_int64>(0);
        ASSERT_TRUE(v) << v.get_message();
        EXPECT_EQ(*v, 1);

        auto end = next_ok(*cur);
        EXPECT_TRUE(end.is_empty()); // SQLITE_DONE → 빈 row
    }

    TEST_F(CursorTest, EmptyResultReturnsEmptyRow) {
        exec("CREATE TABLE t (id INTEGER);");
        auto cur = make_cursor("SELECT id FROM t");
        ASSERT_NE(cur, nullptr);
        EXPECT_TRUE(next_ok(*cur).is_empty());
    }

    TEST_F(CursorTest, ReadsAllRowsInOrder) {
        auto cur = make_cursor(seq_sql(100));
        ASSERT_NE(cur, nullptr);

        std::int64_t expected = 1;
        while (true) {
            auto row = next_ok(*cur);
            if (row.is_empty()) break;
            auto v = row.get<csql::sqlite_int64>(0);
            ASSERT_TRUE(v) << v.get_message();
            EXPECT_EQ(*v, expected);
            ++expected;
        }
        EXPECT_EQ(expected, 101);
    }

    TEST_F(CursorTest, RowOwnsValuesAfterNextStep) {
        // 앞 줄의 text/blob 포인터는 다음 step 에서 무효가 된다 → row 는 복사본을 가져야 함
        auto cur = make_cursor(
            "SELECT 'first', x'0a0b' UNION ALL SELECT 'second', x'0c0d0e'");
        ASSERT_NE(cur, nullptr);

        auto first = next_ok(*cur);
        auto second = next_ok(*cur);
        auto end = next_ok(*cur);
        EXPECT_TRUE(end.is_empty());

        auto t1 = first.get<csql::sqlite_text>(0);
        auto b1 = first.get<csql::sqlite_blob>(1);
        auto t2 = second.get<csql::sqlite_text>(0);
        auto b2 = second.get<csql::sqlite_blob>(1);
        ASSERT_TRUE(t1 && b1 && t2 && b2);
        EXPECT_EQ(*t1, "first");
        EXPECT_EQ(*b1, (csql::sqlite_blob{0x0a, 0x0b}));
        EXPECT_EQ(*t2, "second");
        EXPECT_EQ(*b2, (csql::sqlite_blob{0x0c, 0x0d, 0x0e}));
    }

    // ───────────────────────── 컬럼 타입 매핑 ─────────────────────────

    TEST_F(CursorTest, MapsEachColumnType) {
        auto cur = make_cursor("SELECT 42, 3.5, 'abc', x'0102ff', NULL");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);

        auto i = row.get<csql::sqlite_int64>(0);
        auto d = row.get<csql::sqlite_double>(1);
        auto t = row.get<csql::sqlite_text>(2);
        auto b = row.get<csql::sqlite_blob>(3);
        ASSERT_TRUE(i && d && t && b);
        EXPECT_EQ(*i, 42);
        EXPECT_DOUBLE_EQ(*d, 3.5);
        EXPECT_EQ(*t, "abc");
        EXPECT_EQ(*b, (csql::sqlite_blob{0x01, 0x02, 0xff}));

        EXPECT_TRUE(row.is_null(4));
        for (std::size_t k = 0; k < 4; ++k) EXPECT_FALSE(row.is_null(k)) << "index=" << k;
    }

    TEST_F(CursorTest, WrongTypeAndOutOfRangeFail) {
        auto cur = make_cursor("SELECT 42, 'abc', NULL");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);

        auto as_text = row.get<csql::sqlite_text>(0);
        EXPECT_FALSE(as_text);
        EXPECT_EQ(as_text.get_message(), "type mismatch");

        auto as_int = row.get<csql::sqlite_int64>(1);
        EXPECT_FALSE(as_int);

        auto from_null = row.get<csql::sqlite_int64>(2);
        EXPECT_FALSE(from_null);
        EXPECT_EQ(from_null.get_message(), "null");

        auto oor = row.get<csql::sqlite_int64>(3); // 컬럼 3개 → index 3 은 범위 밖
        EXPECT_FALSE(oor);
        EXPECT_EQ(oor.get_message(), "out of range");
        EXPECT_FALSE(row.is_null(3));
    }

    TEST_F(CursorTest, IntegerColumnReadableAsDouble) {
        auto cur = make_cursor("SELECT 7");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);
        auto d = row.get<csql::sqlite_double>(0);
        ASSERT_TRUE(d) << d.get_message();
        EXPECT_DOUBLE_EQ(*d, 7.0);
    }

    TEST_F(CursorTest, Int64Boundaries) {
        auto cur = make_cursor("SELECT 9223372036854775807, -9223372036854775807 - 1, 0");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);

        auto mx = row.get<csql::sqlite_int64>(0);
        auto mn = row.get<csql::sqlite_int64>(1);
        auto zero = row.get<csql::sqlite_int64>(2);
        ASSERT_TRUE(mx && mn && zero);
        EXPECT_EQ(*mx, std::numeric_limits<std::int64_t>::max());
        EXPECT_EQ(*mn, std::numeric_limits<std::int64_t>::min());
        EXPECT_EQ(*zero, 0);
    }

    TEST_F(CursorTest, TextKeepsEmbeddedNulAndUtf8) {
        // 'a\0b' : sqlite3_column_bytes 로 길이를 받아야 \0 뒤까지 살아남는다
        auto cur = make_cursor("SELECT CAST(x'610062' AS TEXT), '차오 🙂'");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);

        auto nul = row.get<csql::sqlite_text>(0);
        auto ko = row.get<csql::sqlite_text>(1);
        ASSERT_TRUE(nul && ko);
        EXPECT_EQ(*nul, std::string("a\0b", 3));
        EXPECT_EQ((*nul).size(), 3u);
        EXPECT_EQ(*ko, "차오 🙂");
    }

    TEST_F(CursorTest, EmptyTextIsTextNotNull) {
        auto cur = make_cursor("SELECT ''");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);
        EXPECT_FALSE(row.is_null(0));
        auto t = row.get<csql::sqlite_text>(0);
        ASSERT_TRUE(t) << t.get_message();
        EXPECT_TRUE((*t).empty());
    }

    TEST_F(CursorTest, EmptyBlobIsBlobNotNull) {
        // 길이 0 blob 은 sqlite3_column_blob 이 nullptr → else 분기(sqlite_blob{}) 검증
        auto cur = make_cursor("SELECT x'', zeroblob(0)");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);
        for (std::size_t k = 0; k < 2; ++k) {
            EXPECT_FALSE(row.is_null(k)) << "index=" << k;
            auto b = row.get<csql::sqlite_blob>(k);
            ASSERT_TRUE(b) << "index=" << k << " " << b.get_message();
            EXPECT_TRUE((*b).empty());
        }
    }

    TEST_F(CursorTest, ZeroBlobAndLargeBlob) {
        auto cur = make_cursor("SELECT zeroblob(4), randomblob(100000)");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);

        auto z = row.get<csql::sqlite_blob>(0);
        auto big = row.get<csql::sqlite_blob>(1);
        ASSERT_TRUE(z && big);
        EXPECT_EQ(*z, (csql::sqlite_blob{0, 0, 0, 0}));
        EXPECT_EQ((*big).size(), 100000u);
    }

    TEST_F(CursorTest, StoredColumnTypesFollowAffinity) {
        // 컬럼 선언 타입이 아니라 "저장된 값 타입" 으로 매핑되는지
        exec("CREATE TABLE t (a INTEGER, b REAL, c TEXT, d BLOB, e);"
             "INSERT INTO t VALUES ('12', 1, 34, x'ff', 5.5);");
        auto cur = make_cursor("SELECT a, b, c, d, e FROM t");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);

        auto a = row.get<csql::sqlite_int64>(0);  // '12' → INTEGER affinity
        auto b = row.get<csql::sqlite_double>(1); // 1 → REAL affinity
        auto c = row.get<csql::sqlite_text>(2);   // 34 → TEXT affinity
        auto d = row.get<csql::sqlite_blob>(3);
        auto e = row.get<csql::sqlite_double>(4);
        ASSERT_TRUE(a && b && c && d && e);
        EXPECT_EQ(*a, 12);
        EXPECT_DOUBLE_EQ(*b, 1.0);
        EXPECT_EQ(*c, "34");
        EXPECT_EQ(*d, (csql::sqlite_blob{0xff}));
        EXPECT_DOUBLE_EQ(*e, 5.5);
    }

    // [2026-11-09] 같은 컬럼이라도 줄마다 저장 타입이 다르면 줄마다 다르게 매핑된다
    TEST_F(CursorTest, ColumnTypeMayDifferPerRow) {
        auto cur = make_cursor("SELECT 1 UNION ALL SELECT 'a' UNION ALL SELECT NULL "
                               "UNION ALL SELECT 2.5 UNION ALL SELECT x'01'");
        ASSERT_NE(cur, nullptr);

        auto r1 = next_ok(*cur);
        auto r2 = next_ok(*cur);
        auto r3 = next_ok(*cur);
        auto r4 = next_ok(*cur);
        auto r5 = next_ok(*cur);
        EXPECT_TRUE(next_ok(*cur).is_empty());

        auto i = r1.get<csql::sqlite_int64>(0);
        auto t = r2.get<csql::sqlite_text>(0);
        auto d = r4.get<csql::sqlite_double>(0);
        auto b = r5.get<csql::sqlite_blob>(0);
        ASSERT_TRUE(i && t && d && b);
        EXPECT_EQ(*i, 1);
        EXPECT_EQ(*t, "a");
        EXPECT_TRUE(r3.is_null(0));
        EXPECT_DOUBLE_EQ(*d, 2.5);
        EXPECT_EQ(*b, (csql::sqlite_blob{0x01}));
    }

    // [2026-11-09] int → double 변환은 되지만 반대(double → int)는 안 된다
    TEST_F(CursorTest, DoubleColumnNotReadableAsInt) {
        auto cur = make_cursor("SELECT 2.0");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);
        auto v = row.get<csql::sqlite_int64>(0);
        EXPECT_FALSE(v);
        EXPECT_EQ(v.get_message(), "type mismatch");
    }

    // [2026-11-09] NULL 컬럼은 어떤 타입으로 읽어도 "null" 실패 (double 변환 분기보다 null 검사가 먼저)
    TEST_F(CursorTest, NullFailsForEveryType) {
        auto cur = make_cursor("SELECT NULL");
        ASSERT_NE(cur, nullptr);
        auto row = next_ok(*cur);
        EXPECT_EQ(row.get<csql::sqlite_int64>(0).get_message(), "null");
        EXPECT_EQ(row.get<csql::sqlite_double>(0).get_message(), "null");
        EXPECT_EQ(row.get<csql::sqlite_text>(0).get_message(), "null");
        EXPECT_EQ(row.get<csql::sqlite_blob>(0).get_message(), "null");
        EXPECT_FALSE(row.get<csql::sqlite_null>(0)); // sqlite_null 로 꺼내도 실패 → is_null 로 확인해야 함
    }

    // [2026-11-09] DONE 으로 받은 빈 row 는 get 하면 out of range, is_null 은 false
    TEST_F(CursorTest, EndRowGetIsOutOfRange) {
        auto cur = make_cursor("SELECT 1 WHERE 0");
        ASSERT_NE(cur, nullptr);
        auto end = next_ok(*cur);
        ASSERT_TRUE(end.is_empty());
        auto v = end.get<csql::sqlite_int64>(0);
        EXPECT_FALSE(v);
        EXPECT_EQ(v.get_message(), "out of range");
        EXPECT_FALSE(end.is_null(0));
    }

    // [2026-11-09] cursor 에 넘기기 전에 bind 한 값이 그대로 읽힌다
    TEST_F(CursorTest, ReadsPreBoundParameters) {
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(_conn->get_db(), "SELECT ?1, ?2, ?3", -1, &stmt, nullptr),
                  SQLITE_OK);
        csql::sqlite3_stmt_ptr guard{stmt};
        ASSERT_EQ(sqlite3_bind_int64(stmt, 1, 77), SQLITE_OK);
        ASSERT_EQ(sqlite3_bind_text(stmt, 2, "ciao", -1, SQLITE_TRANSIENT), SQLITE_OK);
        // ?3 은 bind 안 함 → NULL

        csql::cursor cur{_conn, std::move(guard)};
        auto row = next_ok(cur);
        auto i = row.get<csql::sqlite_int64>(0);
        auto t = row.get<csql::sqlite_text>(1);
        ASSERT_TRUE(i && t);
        EXPECT_EQ(*i, 77);
        EXPECT_EQ(*t, "ciao");
        EXPECT_TRUE(row.is_null(2));
        EXPECT_TRUE(next_ok(cur).is_empty());
    }

    // [2026-11-09] 결과 컬럼이 없는 문장(DDL)도 cursor 로 실행된다 → 바로 DONE
    TEST_F(CursorTest, DdlStatementRunsAndEnds) {
        {
            auto cur = make_cursor("CREATE TABLE t (id INTEGER)");
            ASSERT_NE(cur, nullptr);
            EXPECT_TRUE(next_ok(*cur).is_empty());
            EXPECT_TRUE(next_ok(*cur).is_empty()); // 다시 불러도 재실행(table exists 에러) 안 됨
        }
        EXPECT_EQ(count(*this, "SELECT COUNT(*) FROM sqlite_schema WHERE name = 't'"), 1);
    }

    // [2026-11-09] row 를 move 해도 값이 그대로 옮겨진다
    TEST_F(CursorTest, RowMoveKeepsValues) {
        auto cur = make_cursor("SELECT 'moved', 5");
        ASSERT_NE(cur, nullptr);
        auto src = next_ok(*cur);

        csql::sqlite_row dst{std::move(src)};
        auto t = dst.get<csql::sqlite_text>(0);
        auto i = dst.get<csql::sqlite_int64>(1);
        ASSERT_TRUE(t && i);
        EXPECT_EQ(*t, "moved");
        EXPECT_EQ(*i, 5);

        csql::sqlite_row assigned;
        assigned = std::move(dst);
        EXPECT_FALSE(assigned.is_empty());
    }

    // ───────────────────────── 끝난 뒤 동작 ─────────────────────────

    TEST_F(CursorTest, NextAfterDoneDoesNotRestartSelect) {
        auto cur = make_cursor(seq_sql(2));
        ASSERT_NE(cur, nullptr);
        EXPECT_FALSE(next_ok(*cur).is_empty());
        EXPECT_FALSE(next_ok(*cur).is_empty());
        for (int k = 0; k < 3; ++k) {
            // _is_end 가 없으면 sqlite auto-reset 으로 1 부터 다시 나온다
            EXPECT_TRUE(next_ok(*cur).is_empty()) << "call=" << k;
        }
    }

    TEST_F(CursorTest, NextAfterDoneDoesNotReExecuteInsert) {
        exec("CREATE TABLE t (id INTEGER);");
        {
            auto cur = make_cursor("INSERT INTO t VALUES (1)");
            ASSERT_NE(cur, nullptr);
            EXPECT_TRUE(next_ok(*cur).is_empty()); // INSERT 는 바로 DONE
            EXPECT_TRUE(next_ok(*cur).is_empty());
            EXPECT_TRUE(next_ok(*cur).is_empty());
        }
        EXPECT_EQ(count(*this, "SELECT COUNT(*) FROM t"), 1);
    }

    TEST_F(CursorTest, InsertReturningYieldsRow) {
        exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);");
        auto cur = make_cursor("INSERT INTO t (name) VALUES ('a'), ('b') RETURNING id, name");
        ASSERT_NE(cur, nullptr);

        // RETURNING 순서는 sqlite 문서상 보장되지 않음 → 정렬해서 비교
        std::vector<std::pair<std::int64_t, std::string>> got;
        for (int k = 0; k < 2; ++k) {
            auto row = next_ok(*cur);
            auto id = row.get<csql::sqlite_int64>(0);
            auto name = row.get<csql::sqlite_text>(1);
            ASSERT_TRUE(id && name);
            got.emplace_back(*id, *name);
        }
        EXPECT_TRUE(next_ok(*cur).is_empty());

        std::ranges::sort(got);
        const std::vector<std::pair<std::int64_t, std::string>> expected{{1, "a"}, {2, "b"}};
        EXPECT_EQ(got, expected);
    }

    // ───────────────────────── 에러 ─────────────────────────

    TEST_F(CursorTest, ConstraintViolationFails) {
        exec("CREATE TABLE t (id INTEGER PRIMARY KEY); INSERT INTO t VALUES (1);");
        auto cur = make_cursor("INSERT INTO t VALUES (1)");
        ASSERT_NE(cur, nullptr);

        auto r = cur->next();
        ASSERT_FALSE(r);
        EXPECT_NE(r.get_message().find("failed step stmt"), std::string::npos) << r.get_message();
        EXPECT_NE(r.get_message().find("UNIQUE"), std::string::npos) << r.get_message();
    }

    TEST_F(CursorTest, RuntimeErrorMidIteration) {
        // 3번째 줄에서 json() 이 malformed JSON 에러를 낸다
        auto cur = make_cursor(
            "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 5) "
            "SELECT x, json(CASE WHEN x = 3 THEN '{bad' ELSE '1' END) FROM c");
        ASSERT_NE(cur, nullptr);

        for (std::int64_t expected = 1; expected <= 2; ++expected) {
            auto row = next_ok(*cur);
            auto v = row.get<csql::sqlite_int64>(0);
            ASSERT_TRUE(v) << v.get_message();
            EXPECT_EQ(*v, expected);
        }

        auto r = cur->next();
        ASSERT_FALSE(r);
        EXPECT_NE(r.get_message().find("malformed JSON"), std::string::npos) << r.get_message();
    }

    // [2026-11-09] sqlite 는 에러 뒤 step 을 다시 부르면 auto-reset 해서 처음부터 다시 돈다 (vcpkg sqlite 실측).
    //              cursor 가 에러 상태(_error)를 기억해서 재실행을 막는지 확인하는 테스트.
    //              (macOS SDK sqlite 는 SQLITE_OMIT_AUTORESET 류 빌드라 다시 안 돌 수 있음 → 아래 tick 테스트로 보완)
    TEST_F(CursorTest, NextAfterErrorDoesNotRestart) {
        auto cur = make_cursor(
            "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 5) "
            "SELECT x, json(CASE WHEN x = 3 THEN '{bad' ELSE '1' END) FROM c");
        ASSERT_NE(cur, nullptr);
        next_ok(*cur);
        next_ok(*cur);
        auto first = cur->next();
        ASSERT_FALSE(first);

        for (int k = 0; k < 3; ++k) {
            auto again = cur->next();
            // 빈 row(끝) 도, "x = 1" 줄도 아닌 "같은 에러" 가 계속 나와야 한다
            ASSERT_FALSE(again) << "에러 후 커서가 다시 진행됨 call=" << k;
            EXPECT_EQ(again.get_message(), first.get_message()) << "call=" << k;
        }
    }

    // ───────────────────────── 에러 상태 (tick 함수로 step 횟수 측정) ─────────────────────────
    // [2026-11-09] tick(x) : 호출될 때마다 카운터 +1, x == fail_at 이면 "boom" 에러.
    //   sqlite 빌드 옵션(auto-reset 여부)과 상관없이 "step 이 실제로 다시 돌았는지" 를 직접 센다.

    struct tick_state {
        std::atomic<int> calls{0};
        std::int64_t fail_at = -1;
    };

    void tick_fn(sqlite3_context* ctx, int /*argc*/, sqlite3_value** argv) {
        auto* st = static_cast<tick_state*>(sqlite3_user_data(ctx));
        st->calls.fetch_add(1);
        const auto x = sqlite3_value_int64(argv[0]);
        if (x == st->fail_at) {
            sqlite3_result_error(ctx, "boom", -1);
            return;
        }
        sqlite3_result_int64(ctx, x);
    }

    class CursorTickTest : public CursorTest {
    protected:
        void SetUp() override {
            CursorTest::SetUp();
            ASSERT_NE(_conn, nullptr); // 부모 SetUp 의 ASSERT 실패는 여기까지 전파되지 않는다
            ASSERT_EQ(sqlite3_create_function_v2(_conn->get_db(), "tick", 1, SQLITE_UTF8, &_tick,
                                                 &tick_fn, nullptr, nullptr, nullptr),
                      SQLITE_OK);
        }

        // 1 ~ n 을 tick() 에 통과시키는 SELECT
        static std::string tick_sql(const int n) { return "SELECT tick(x) FROM (" + seq_sql(n) + ")"; }

        tick_state _tick;
    };

    TEST_F(CursorTickTest, NextAfterDoneDoesNotReStep) {
        auto cur = make_cursor(tick_sql(2));
        ASSERT_NE(cur, nullptr);
        EXPECT_FALSE(next_ok(*cur).is_empty());
        EXPECT_FALSE(next_ok(*cur).is_empty());
        EXPECT_TRUE(next_ok(*cur).is_empty());
        ASSERT_EQ(_tick.calls.load(), 2);

        for (int k = 0; k < 3; ++k) EXPECT_TRUE(next_ok(*cur).is_empty()) << "call=" << k;
        EXPECT_EQ(_tick.calls.load(), 2) << "DONE 뒤에 step 이 다시 실행됨";
    }

    TEST_F(CursorTickTest, ErrorIsStickyAndDoesNotReStep) {
        _tick.fail_at = 3;
        auto cur = make_cursor(tick_sql(5));
        ASSERT_NE(cur, nullptr);
        next_ok(*cur);
        next_ok(*cur);

        auto first = cur->next();
        ASSERT_FALSE(first);
        EXPECT_NE(first.get_message().find("failed step stmt"), std::string::npos) << first.get_message();
        EXPECT_NE(first.get_message().find("boom"), std::string::npos) << first.get_message();
        ASSERT_EQ(_tick.calls.load(), 3);

        for (int k = 0; k < 3; ++k) {
            auto again = cur->next();
            ASSERT_FALSE(again) << "call=" << k;
            EXPECT_EQ(again.get_message(), first.get_message()) << "call=" << k;
        }
        EXPECT_EQ(_tick.calls.load(), 3) << "에러 뒤에 step 이 다시 실행됨 (_error 미설정?)";
    }

    TEST_F(CursorTickTest, ErrorMessageSurvivesOtherStatements) {
        // sqlite3_errmsg 는 커넥션 단위라 다른 문장을 돌리면 내용이 바뀐다 ("another row available" 등)
        // → cursor 는 에러 시점의 메시지를 복사해 두어야 한다
        _tick.fail_at = 1;
        auto bad = make_cursor(tick_sql(3));
        ASSERT_NE(bad, nullptr);
        auto first = bad->next();
        ASSERT_FALSE(first);

        exec("CREATE TABLE ok (id INTEGER); INSERT INTO ok VALUES (1);");
        auto good = make_cursor("SELECT id FROM ok");
        ASSERT_NE(good, nullptr);
        EXPECT_FALSE(next_ok(*good).is_empty());
        // 전제: 커넥션의 현재 errmsg 에는 더 이상 "boom" 이 없다
        ASSERT_EQ(std::string{sqlite3_errmsg(_conn->get_db())}.find("boom"), std::string::npos);

        auto again = bad->next();
        ASSERT_FALSE(again);
        EXPECT_EQ(again.get_message(), first.get_message());
    }

    TEST_F(CursorTickTest, ErrorDoesNotBreakOtherCursors) {
        _tick.fail_at = 2;
        auto bad = make_cursor(tick_sql(3));
        auto good = make_cursor(seq_sql(3));
        ASSERT_NE(bad, nullptr);
        ASSERT_NE(good, nullptr);

        next_ok(*bad);
        ASSERT_FALSE(bad->next());

        // 같은 커넥션의 다른 cursor 는 영향 없이 끝까지 읽힌다
        std::int64_t sum = 0;
        while (true) {
            auto row = next_ok(*good);
            if (row.is_empty()) break;
            auto v = row.get<csql::sqlite_int64>(0);
            ASSERT_TRUE(v) << v.get_message();
            sum += *v;
        }
        EXPECT_EQ(sum, 6);
    }

    TEST_F(CursorTickTest, MovePreservesErrorState) {
        _tick.fail_at = 1;
        auto src = make_cursor(tick_sql(3));
        ASSERT_NE(src, nullptr);
        auto first = src->next();
        ASSERT_FALSE(first);

        csql::cursor moved{std::move(*src)};
        src.reset();

        auto again = moved.next();
        ASSERT_FALSE(again);
        EXPECT_EQ(again.get_message(), first.get_message());
        EXPECT_EQ(_tick.calls.load(), 1);
    }

    TEST_F(CursorTickTest, ErrorOnFirstStepOfInsertDoesNotRetry) {
        // INSERT ... SELECT tick(x) 가 첫 step 에서 실패 → 재시도하면 tick 이 다시 불린다
        exec("CREATE TABLE t (v INTEGER);");
        _tick.fail_at = 2;
        {
            auto cur = make_cursor("INSERT INTO t " + tick_sql(3));
            ASSERT_NE(cur, nullptr);
            ASSERT_FALSE(cur->next());
            const int after_first = _tick.calls.load();
            ASSERT_FALSE(cur->next());
            ASSERT_FALSE(cur->next());
            EXPECT_EQ(_tick.calls.load(), after_first);
        }
        // 문장 단위 롤백 → 1 도 남지 않는다
        EXPECT_EQ(count(*this, "SELECT COUNT(*) FROM t"), 0);
    }

    // ───────────────────────── mutex ─────────────────────────

    TEST_F(CursorTest, NextWaitsForConnectionMutex) {
        auto cur = make_cursor("SELECT 1");
        ASSERT_NE(cur, nullptr);

        std::unique_lock lock{_conn->get_mutex()};
        auto fut = std::async(std::launch::async, [&cur] { return cur->next(); });
        EXPECT_EQ(fut.wait_for(200ms), std::future_status::timeout) << "mutex 를 안 잡고 진행함";

        lock.unlock();
        ASSERT_EQ(fut.wait_for(2s), std::future_status::ready);
        auto r = fut.get();
        ASSERT_TRUE(r) << r.get_message();
        EXPECT_FALSE((*r).is_empty());
    }

    TEST_F(CursorTest, NextReleasesMutexAfterReturn) {
        auto cur = make_cursor(seq_sql(3));
        ASSERT_NE(cur, nullptr);
        next_ok(*cur);

        // 같은 스레드 try_lock 은 UB 라서 다른 스레드에서 확인
        auto locked = std::async(std::launch::async, [this] {
            std::unique_lock l{_conn->get_mutex(), std::try_to_lock};
            return l.owns_lock();
        });
        EXPECT_TRUE(locked.get());
    }

    TEST_F(CursorTest, InterleavedCursorsOnSameConnection) {
        auto a = make_cursor(seq_sql(3));
        auto b = make_cursor("SELECT x * 10 FROM (" + seq_sql(3) + ")");
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);

        for (std::int64_t k = 1; k <= 3; ++k) {
            auto ra = next_ok(*a);
            auto rb = next_ok(*b);
            auto va = ra.get<csql::sqlite_int64>(0);
            auto vb = rb.get<csql::sqlite_int64>(0);
            ASSERT_TRUE(va && vb);
            EXPECT_EQ(*va, k);
            EXPECT_EQ(*vb, k * 10);
        }
        EXPECT_TRUE(next_ok(*a).is_empty());
        EXPECT_TRUE(next_ok(*b).is_empty());
    }

    TEST_F(CursorTest, ConcurrentCursorsOnSameConnection) {
        constexpr int kThreads = 4;
        constexpr int kRows = 500;

        std::vector<std::unique_ptr<csql::cursor>> cursors;
        for (int t = 0; t < kThreads; ++t) {
            cursors.push_back(make_cursor(seq_sql(kRows)));
            ASSERT_NE(cursors.back(), nullptr);
        }

        std::vector<std::future<std::int64_t>> sums;
        for (auto& cur : cursors) {
            sums.push_back(std::async(std::launch::async, [&cur] {
                std::int64_t sum = 0;
                while (true) {
                    auto r = cur->next();
                    if (!r) return std::int64_t{-1};
                    if ((*r).is_empty()) return sum;
                    auto v = (*r).get<csql::sqlite_int64>(0);
                    if (!v) return std::int64_t{-1};
                    sum += *v;
                }
            }));
        }

        constexpr std::int64_t expected = std::int64_t{kRows} * (kRows + 1) / 2;
        for (auto& s : sums) EXPECT_EQ(s.get(), expected);
    }

    // ───────────────────────── 수명 / move ─────────────────────────

    TEST_F(CursorTest, CursorKeepsConnectionAlive) {
        auto cur = make_cursor(seq_sql(2));
        ASSERT_NE(cur, nullptr);
        next_ok(*cur);

        _conn.reset(); // 외부 소유자 제거 → cursor 만 남음
        EXPECT_EQ(_released->load(), 0);

        auto row = next_ok(*cur); // 커넥션이 살아 있어야 계속 읽힘
        auto v = row.get<csql::sqlite_int64>(0);
        ASSERT_TRUE(v) << v.get_message();
        EXPECT_EQ(*v, 2);

        cur.reset(); // 마지막 소유자 → stmt finalize 후 release
        EXPECT_EQ(_released->load(), 1);
        EXPECT_EQ(_close_rc->load(), SQLITE_OK) << "stmt 보다 커넥션이 먼저 닫힘";
    }

    TEST_F(CursorTest, DestroyBeforeDoneFinalizesStmt) {
        {
            auto cur = make_cursor(seq_sql(10));
            ASSERT_NE(cur, nullptr);
            next_ok(*cur); // 중간에 버림
        }
        // TearDown 에서 sqlite3_close == SQLITE_OK 로 finalize 여부 확인
        EXPECT_EQ(sqlite3_next_stmt(_conn->get_db(), nullptr), nullptr);
    }

    TEST_F(CursorTest, MoveConstructContinuesIteration) {
        auto src = make_cursor(seq_sql(3));
        ASSERT_NE(src, nullptr);
        next_ok(*src); // 1

        csql::cursor moved{std::move(*src)};
        src.reset(); // move 된 껍데기 소멸 → stmt 가 finalize 되면 안 됨

        auto row = next_ok(moved);
        auto v = row.get<csql::sqlite_int64>(0);
        ASSERT_TRUE(v) << v.get_message();
        EXPECT_EQ(*v, 2);
        EXPECT_FALSE(next_ok(moved).is_empty()); // 3
        EXPECT_TRUE(next_ok(moved).is_empty());
    }

    TEST_F(CursorTest, MoveAssignReplacesStatement) {
        auto a = make_cursor(seq_sql(5));
        auto b = make_cursor("SELECT 'b'");
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        next_ok(*a);

        *a = std::move(*b); // a 의 기존 stmt 는 finalize, b 의 stmt 를 넘겨받음
        b.reset();

        auto row = next_ok(*a);
        auto t = row.get<csql::sqlite_text>(0);
        ASSERT_TRUE(t) << t.get_message();
        EXPECT_EQ(*t, "b");
        EXPECT_TRUE(next_ok(*a).is_empty());

        a.reset();
        EXPECT_EQ(sqlite3_next_stmt(_conn->get_db(), nullptr), nullptr);
    }

    TEST_F(CursorTest, MovePreservesEndState) {
        auto src = make_cursor(seq_sql(1));
        ASSERT_NE(src, nullptr);
        next_ok(*src);
        EXPECT_TRUE(next_ok(*src).is_empty()); // DONE → _is_end

        csql::cursor moved{std::move(*src)};
        src.reset();
        EXPECT_TRUE(next_ok(moved).is_empty()); // 다시 처음부터 돌면 안 됨
    }
} // namespace
