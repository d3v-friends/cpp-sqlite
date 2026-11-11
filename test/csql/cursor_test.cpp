#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "csql/cursor.hpp"
#include "csql/database.hpp"
#include "csql/query.hpp"
#include "csql/sqlite_row.hpp"
#include "test_helper.hpp"

class CursorTest : public ::testing::Test {
protected:
    std::shared_ptr<csql::database> db;

    void SetUp() override {
        db = csql::test::open_memory_db();
        ASSERT_TRUE(db != nullptr);
        auto r = csql::exec(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, k TEXT UNIQUE)");
        ASSERT_TRUE(r) << r.get_message();
    }

    csql::sqlite_int64 count_rows() {
        auto cur = csql::query(db, "SELECT count(*) FROM t");
        if (!cur) {
            return -1;
        }
        auto row = (*cur)->next();
        if (!row) {
            return -1;
        }
        auto v = row->get<csql::sqlite_int64>(0);
        return v ? *v : -1;
    }
};

TEST_F(CursorTest, IteratesAllRowsThenEmpty) {
    ASSERT_TRUE(csql::exec(db, "INSERT INTO t(k) VALUES ('a'), ('b'), ('c')"));

    auto cur = csql::query(db, "SELECT k FROM t ORDER BY id");
    ASSERT_TRUE(cur);

    std::string joined;
    while (true) {
        auto row = (*cur)->next();
        ASSERT_TRUE(row) << row.get_message();
        if (row->is_empty()) {
            break;
        }
        joined += *row->get<csql::sqlite_text>(0);
    }
    EXPECT_EQ(joined, std::string{"abc"});
    EXPECT_TRUE((*cur)->is_end());
}

TEST_F(CursorTest, EmptyResultIsEndImmediately) {
    auto cur = csql::query(db, "SELECT k FROM t");
    ASSERT_TRUE(cur);
    EXPECT_FALSE((*cur)->is_end());
    auto row = (*cur)->next();
    ASSERT_TRUE(row);
    EXPECT_TRUE(row->is_empty());
    EXPECT_TRUE((*cur)->is_end());
}

TEST_F(CursorTest, NextAfterDoneDoesNotReExecute) {
    // 이전 버그: DONE 뒤 step 하면 SQLite 가 자동 reset 해서 INSERT 가 다시 실행됨
    auto cur = csql::query(db, "INSERT INTO t(k) VALUES (?)", "once");
    ASSERT_TRUE(cur);

    for (int i = 0; i < 3; ++i) {
        auto row = (*cur)->next();
        ASSERT_TRUE(row) << row.get_message();
        EXPECT_TRUE(row->is_empty());
    }
    EXPECT_TRUE((*cur)->is_end());
    EXPECT_EQ(count_rows(), 1);
}

TEST_F(CursorTest, NextAfterDoneDoesNotReExecuteWithoutUnique) {
    // UNIQUE 가 없는 테이블에서도 한 번만 들어가야 한다
    ASSERT_TRUE(csql::exec(db, "CREATE TABLE n(x)"));
    auto cur = csql::query(db, "INSERT INTO n(x) VALUES (1)");
    ASSERT_TRUE(cur);
    for (int i = 0; i < 3; ++i) {
        auto row = (*cur)->next();
        ASSERT_TRUE(row);
    }

    auto cnt = csql::query(db, "SELECT count(*) FROM n");
    ASSERT_TRUE(cnt);
    auto row = (*cnt)->next();
    ASSERT_TRUE(row);
    EXPECT_EQ(*row->get<csql::sqlite_int64>(0), 1);
}

TEST_F(CursorTest, StepFailureReturnsErrorThenEnds) {
    // 이전 버그: step 실패 result 를 is_empty() 로 역참조
    ASSERT_TRUE(csql::exec(db, "INSERT INTO t(k) VALUES ('dup')"));

    auto cur = csql::query(db, "INSERT INTO t(k) VALUES ('dup')");
    ASSERT_TRUE(cur);

    auto first = (*cur)->next();
    ASSERT_FALSE(first);
    EXPECT_NE(first.get_message().find("UNIQUE"), std::string::npos) << first.get_message();
    EXPECT_TRUE((*cur)->is_end());

    // 실패 뒤에는 다시 step 하지 않고 빈 행만 준다
    auto second = (*cur)->next();
    ASSERT_TRUE(second);
    EXPECT_TRUE(second->is_empty());
    EXPECT_EQ(count_rows(), 1);
}

TEST_F(CursorTest, ExecReturnsStepError) {
    ASSERT_TRUE(csql::exec(db, "INSERT INTO t(k) VALUES ('x')"));
    auto r = csql::exec(db, "INSERT INTO t(k) VALUES (?)", "x");
    ASSERT_FALSE(r);
    EXPECT_NE(r.get_message().find("failed step stmt"), std::string::npos) << r.get_message();
}

TEST_F(CursorTest, CursorKeepsConnectionAlive) {
    // cursor 가 database 를 공유하므로 원래 포인터를 놓아도 쓸 수 있다
    bool released = false;
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &raw), SQLITE_OK);
    auto local = std::make_shared<csql::database>(raw, [&released](sqlite3* p) {
        sqlite3_close_v2(p);
        released = true;
    });

    auto cur = csql::query(local, "SELECT 7");
    ASSERT_TRUE(cur);
    local.reset();
    EXPECT_FALSE(released);

    auto row = (*cur)->next();
    ASSERT_TRUE(row);
    EXPECT_EQ(*row->get<csql::sqlite_int64>(0), 7);

    cur->reset(); // 커서가 사라질 때 반납
    EXPECT_TRUE(released);
}
