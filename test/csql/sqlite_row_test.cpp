#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "csql/sqlite_row.hpp"
#include "csql/types.hpp"

namespace {
    // get<T> 에 쓸 수 있는 타입 확인 (컴파일 타임)
    static_assert(csql::sqlite_value_type<csql::sqlite_text>);
    static_assert(csql::sqlite_value_type<csql::sqlite_int64>);
    static_assert(csql::sqlite_value_type<csql::sqlite_double>);
    static_assert(csql::sqlite_value_type<csql::sqlite_blob>);
    static_assert(!csql::sqlite_value_type<csql::sqlite_null>, "null 은 is_null() 로 확인");
    static_assert(!csql::sqlite_value_type<int>);
    static_assert(!csql::sqlite_value_type<float>);

    csql::sqlite_row make_row() {
        std::vector<csql::sqlite_values> v;
        v.emplace_back(csql::sqlite_int64{10});
        v.emplace_back(csql::sqlite_double{1.5});
        v.emplace_back(csql::sqlite_text{"txt"});
        v.emplace_back(csql::sqlite_blob{1, 2});
        v.emplace_back(csql::sqlite_null{});
        return csql::sqlite_row{std::move(v)};
    }
} // namespace

TEST(SqliteRowTest, DefaultIsEmpty) {
    const csql::sqlite_row row;
    EXPECT_TRUE(row.is_empty());
    EXPECT_EQ(row.size(), 0U);
    EXPECT_FALSE(row.get<csql::sqlite_int64>(0));
}

TEST(SqliteRowTest, GetEachType) {
    const auto row = make_row();
    EXPECT_FALSE(row.is_empty());
    EXPECT_EQ(row.size(), 5U);
    EXPECT_EQ(*row.get<csql::sqlite_int64>(0), 10);
    EXPECT_DOUBLE_EQ(*row.get<csql::sqlite_double>(1), 1.5);
    EXPECT_EQ(*row.get<csql::sqlite_text>(2), std::string{"txt"});
    EXPECT_EQ(*row.get<csql::sqlite_blob>(3), (csql::sqlite_blob{1, 2}));
}

TEST(SqliteRowTest, IntReadAsDouble) {
    const auto row = make_row();
    auto v = row.get<csql::sqlite_double>(0);
    ASSERT_TRUE(v);
    EXPECT_DOUBLE_EQ(*v, 10.0);
}

TEST(SqliteRowTest, DoubleNotReadAsInt) {
    const auto row = make_row();
    auto v = row.get<csql::sqlite_int64>(1);
    ASSERT_FALSE(v);
    EXPECT_EQ(v.get_message(), std::string{"type mismatch"});
}

TEST(SqliteRowTest, TypeMismatch) {
    const auto row = make_row();
    auto v = row.get<csql::sqlite_text>(0);
    ASSERT_FALSE(v);
    EXPECT_EQ(v.get_message(), std::string{"type mismatch"});
}

TEST(SqliteRowTest, NullValue) {
    const auto row = make_row();
    EXPECT_TRUE(row.is_null(4));
    EXPECT_FALSE(row.is_null(0));
    auto v = row.get<csql::sqlite_text>(4);
    ASSERT_FALSE(v);
    EXPECT_EQ(v.get_message(), std::string{"null"});
}

TEST(SqliteRowTest, OutOfRange) {
    const auto row = make_row();
    EXPECT_FALSE(row.is_null(99));
    auto v = row.get<csql::sqlite_int64>(5);
    ASSERT_FALSE(v);
    EXPECT_EQ(v.get_message(), std::string{"out of range"});
}

TEST(SqliteRowTest, MoveKeepsValues) {
    auto a = make_row();
    csql::sqlite_row b{std::move(a)};
    EXPECT_EQ(*b.get<csql::sqlite_int64>(0), 10);
}

TEST(TypesTest, ManagerStatusToString) {
    using enum csql::e_manager_status;
    EXPECT_EQ(csql::to_string(init), std::string{"초기화"});
    EXPECT_EQ(csql::to_string(connecting), std::string{"연결중"});
    EXPECT_EQ(csql::to_string(ready), std::string{"대기중"});
    EXPECT_EQ(csql::to_string(terminating), std::string{"종료중"});
    EXPECT_EQ(csql::to_string(terminated), std::string{"종료"});
}
