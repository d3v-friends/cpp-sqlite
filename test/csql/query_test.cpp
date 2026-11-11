#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "csql/cursor.hpp"
#include "csql/database.hpp"
#include "csql/query.hpp"
#include "csql/sqlite_row.hpp"
#include "test_helper.hpp"

namespace {
    // 락 제거 확인: database 에 get_mutex 가 없어야 한다
    template <class T>
    concept has_get_mutex = requires(T& t) { t.get_mutex(); };
    static_assert(!has_get_mutex<csql::database>, "database must be lock-free");

    // 한 개 값을 넣고 다시 읽는다
    template <class T>
    ctls::result<csql::sqlite_row> round_trip(
        const std::shared_ptr<csql::database>& db, const T& value) {
        if (auto r = csql::exec(db, "DELETE FROM v"); !r) {
            return r.to_failed();
        }
        if (auto r = csql::exec(db, "INSERT INTO v(x) VALUES (?)", value); !r) {
            return r.to_failed();
        }
        auto cur = csql::query(db, "SELECT x FROM v");
        if (!cur) {
            return cur.to_failed();
        }
        return (*cur)->next();
    }
} // namespace

class QueryTest : public ::testing::Test {
protected:
    std::shared_ptr<csql::database> db;

    void SetUp() override {
        db = csql::test::open_memory_db();
        ASSERT_TRUE(db != nullptr);
        auto r = csql::exec(db, "CREATE TABLE v(x)");
        ASSERT_TRUE(r) << r.get_message();
    }
};

// ---------- sql 검사 ----------

TEST_F(QueryTest, NullDatabaseFails) {
    auto r = csql::query(std::shared_ptr<csql::database>{}, "SELECT 1");
    ASSERT_FALSE(r);
    EXPECT_EQ(r.get_message(), std::string{"database is null"});
}

TEST_F(QueryTest, EmptySqlFails) {
    auto r = csql::query(db, "");
    ASSERT_FALSE(r);
    EXPECT_EQ(r.get_message(), std::string{"sql is empty"});
}

TEST_F(QueryTest, WhitespaceOnlySqlFails) {
    // 이전 버그: stmt == nullptr 인 채로 cursor 생성
    for (const std::string sql : {"   ", "\n\t", "-- comment only", "/* c */"}) {
        auto r = csql::query(db, sql);
        ASSERT_FALSE(r) << sql;
        EXPECT_NE(r.get_message().find("sql has no statement"), std::string::npos)
            << r.get_message();
    }
}

TEST_F(QueryTest, MultipleStatementsRejected) {
    // 이전 버그: 두 번째 문장이 조용히 버려짐
    auto r = csql::query(db, "INSERT INTO v(x) VALUES (1); INSERT INTO v(x) VALUES (2)");
    ASSERT_FALSE(r);
    EXPECT_NE(r.get_message().find("multiple statements"), std::string::npos);

    auto cnt = csql::query(db, "SELECT count(*) FROM v");
    ASSERT_TRUE(cnt);
    auto row = (*cnt)->next();
    ASSERT_TRUE(row);
    EXPECT_EQ(*row->get<csql::sqlite_int64>(0), 0); // 하나도 실행되지 않음
}

TEST_F(QueryTest, TrailingSemicolonAndWhitespaceAllowed) {
    for (const std::string sql : {"SELECT 1;", "SELECT 1;  \n", "SELECT 1 ;;"}) {
        auto r = csql::query(db, sql);
        ASSERT_TRUE(r) << sql << " / " << r.get_message();
    }
}

TEST_F(QueryTest, PrepareErrorHasSqliteMessage) {
    auto r = csql::query(db, "SELEC 1");
    ASSERT_FALSE(r);
    EXPECT_NE(r.get_message().find("failed to prepare sql"), std::string::npos);
    EXPECT_NE(r.get_message().find("syntax error"), std::string::npos) << r.get_message();
}

TEST_F(QueryTest, ExecKeepsOriginalError) {
    // 이전 버그: exec 가 "failed to query" 로 원인을 덮어씀
    auto r = csql::exec(db, "SELEC 1");
    ASSERT_FALSE(r);
    EXPECT_NE(r.get_message().find("failed to prepare sql"), std::string::npos)
        << r.get_message();
}

TEST_F(QueryTest, BindCountMismatchFails) {
    auto fewer = csql::query(db, "SELECT ?, ?", 1);
    ASSERT_FALSE(fewer);
    EXPECT_NE(fewer.get_message().find("bind count mismatch"), std::string::npos);

    auto more = csql::query(db, "SELECT ?", 1, 2);
    ASSERT_FALSE(more);
    EXPECT_NE(more.get_message().find("bind count mismatch"), std::string::npos);

    auto none = csql::query(db, "SELECT ?");
    ASSERT_FALSE(none);
}

// ---------- 바인딩 왕복 ----------

TEST_F(QueryTest, BindIntegers) {
    {
        auto row = round_trip(db, 42);
        ASSERT_TRUE(row) << row.get_message();
        EXPECT_EQ(*row->get<csql::sqlite_int64>(0), 42);
    }
    {
        auto row = round_trip(db, std::numeric_limits<std::int64_t>::max());
        ASSERT_TRUE(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>(0), std::numeric_limits<std::int64_t>::max());
    }
    {
        auto row = round_trip(db, std::numeric_limits<std::int64_t>::min());
        ASSERT_TRUE(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>(0), std::numeric_limits<std::int64_t>::min());
    }
    {
        auto row = round_trip(db, std::numeric_limits<std::uint32_t>::max());
        ASSERT_TRUE(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>(0), 4294967295LL);
    }
    {
        // uint8_t 는 char 가 아니라 숫자로 저장된다
        auto row = round_trip(db, std::uint8_t{200});
        ASSERT_TRUE(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>(0), 200);
    }
    {
        auto row = round_trip(db, true);
        ASSERT_TRUE(row);
        EXPECT_EQ(*row->get<csql::sqlite_int64>(0), 1);
    }
}

TEST_F(QueryTest, BindFloatingPoint) {
    auto d = round_trip(db, 3.25);
    ASSERT_TRUE(d);
    EXPECT_DOUBLE_EQ(*d->get<csql::sqlite_double>(0), 3.25);

    auto f = round_trip(db, 0.5f);
    ASSERT_TRUE(f);
    EXPECT_DOUBLE_EQ(*f->get<csql::sqlite_double>(0), 0.5);
}

TEST_F(QueryTest, BindTextKinds) {
    const std::string s = "string";
    const std::string_view sv = "view";
    const char* p = "pointer";

    auto r1 = round_trip(db, s);
    ASSERT_TRUE(r1);
    EXPECT_EQ(*r1->get<csql::sqlite_text>(0), std::string{"string"});

    auto r2 = round_trip(db, sv);
    ASSERT_TRUE(r2);
    EXPECT_EQ(*r2->get<csql::sqlite_text>(0), std::string{"view"});

    auto r3 = round_trip(db, p);
    ASSERT_TRUE(r3);
    EXPECT_EQ(*r3->get<csql::sqlite_text>(0), std::string{"pointer"});

    auto r4 = round_trip(db, "literal");
    ASSERT_TRUE(r4);
    EXPECT_EQ(*r4->get<csql::sqlite_text>(0), std::string{"literal"});

    auto r5 = round_trip(db, std::string{});
    ASSERT_TRUE(r5);
    ASSERT_FALSE(r5->is_null(0)); // 빈 문자열은 NULL 이 아니다
    EXPECT_EQ(*r5->get<csql::sqlite_text>(0), std::string{});
}

TEST_F(QueryTest, BindTextWithEmbeddedNul) {
    const std::string s{"a\0b", 3};
    auto row = round_trip(db, s);
    ASSERT_TRUE(row);
    EXPECT_EQ(row->get<csql::sqlite_text>(0)->size(), 3U);
    EXPECT_EQ(*row->get<csql::sqlite_text>(0), s);
}

TEST_F(QueryTest, BindNulls) {
    auto r1 = round_trip(db, nullptr);
    ASSERT_TRUE(r1);
    EXPECT_TRUE(r1->is_null(0));

    // 이전 버그: string_view(nullptr) UB
    const char* p = nullptr;
    auto r2 = round_trip(db, p);
    ASSERT_TRUE(r2);
    EXPECT_TRUE(r2->is_null(0));
}

TEST_F(QueryTest, BindBlobs) {
    const std::vector<std::uint8_t> blob{0x00, 0xFF, 0x10};
    auto r1 = round_trip(db, blob);
    ASSERT_TRUE(r1);
    EXPECT_EQ(*r1->get<csql::sqlite_blob>(0), blob);

    const std::array<std::uint8_t, 2> arr{0x01, 0x02};
    auto r2 = round_trip(db, arr);
    ASSERT_TRUE(r2);
    EXPECT_EQ(*r2->get<csql::sqlite_blob>(0), (csql::sqlite_blob{0x01, 0x02}));

    auto r3 = round_trip(db, std::vector<std::uint8_t>{});
    ASSERT_TRUE(r3);
    ASSERT_FALSE(r3->is_null(0)); // 길이 0 blob 은 NULL 이 아니다
    EXPECT_TRUE(r3->get<csql::sqlite_blob>(0)->empty());
}

TEST_F(QueryTest, MultipleBindsInOrder) {
    auto cur = csql::query(db, "SELECT ?, ?, ?, ?", 1, 2.5, "three", nullptr);
    ASSERT_TRUE(cur) << cur.get_message();
    auto row = (*cur)->next();
    ASSERT_TRUE(row);
    EXPECT_EQ(row->size(), 4U);
    EXPECT_EQ(*row->get<csql::sqlite_int64>(0), 1);
    EXPECT_DOUBLE_EQ(*row->get<csql::sqlite_double>(1), 2.5);
    EXPECT_EQ(*row->get<csql::sqlite_text>(2), std::string{"three"});
    EXPECT_TRUE(row->is_null(3));
}
