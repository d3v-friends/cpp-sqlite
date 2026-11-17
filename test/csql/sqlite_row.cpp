// csql::sqlite_row 테스트
// - cursor 없이 std::map 으로 sqlite_row 를 바로 만들어서 get / is_null / bind 를 확인한다.
// - cursor.cpp 의 step_stmt 가 넣는 타입(null·text·int64·double·blob)과 같은 값만 쓴다.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ctls/ctls.hpp>

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

    csql::sqlite_row make_row() {
        std::map<std::string, csql::sqlite_values> data;
        data.emplace("id", csql::sqlite_int64{42});
        data.emplace("name", csql::sqlite_text{"chao"});
        data.emplace("score", csql::sqlite_double{1.5});
        data.emplace("payload", csql::sqlite_blob{0x00, 0x01, 0xFF});
        data.emplace("nick", csql::sqlite_null{});
        data.emplace("empty_text", csql::sqlite_text{});
        return csql::sqlite_row{std::move(data)};
    }

    class SqliteRowTest : public ::testing::Test {
    protected:
        csql::sqlite_row row = make_row();
    };

    // ---------------------------------------------------------------- concept
    // 컴파일 시기에 판정되는 것들
    static_assert(csql::sqlite_value_type<csql::sqlite_int64>);
    static_assert(csql::sqlite_value_type<csql::sqlite_text>);
    static_assert(csql::sqlite_value_type<csql::sqlite_double>);
    static_assert(csql::sqlite_value_type<csql::sqlite_blob>);
    static_assert(csql::sqlite_value_type<csql::sqlite_null>);
    static_assert(!csql::sqlite_value_type<int>);
    static_assert(!csql::sqlite_value_type<float>);
    static_assert(!csql::sqlite_value_type<const char*>);
    static_assert(!csql::sqlite_value_type<std::optional<csql::sqlite_int64>>);

    static_assert(csql::sqlite_optional_value_type<std::optional<csql::sqlite_int64>>);
    static_assert(csql::sqlite_optional_value_type<std::optional<csql::sqlite_text>>);
    static_assert(csql::sqlite_optional_value_type<std::optional<csql::sqlite_double>>);
    static_assert(csql::sqlite_optional_value_type<std::optional<csql::sqlite_blob>>);
    static_assert(!csql::sqlite_optional_value_type<std::optional<csql::sqlite_null>>);
    static_assert(!csql::sqlite_optional_value_type<std::optional<int>>);
    static_assert(!csql::sqlite_optional_value_type<csql::sqlite_int64>); // value_type 없음

    static_assert(csql::sqlite_bindable<csql::sqlite_int64>);
    static_assert(csql::sqlite_bindable<std::optional<csql::sqlite_text>>);
    static_assert(!csql::sqlite_bindable<int>);
    static_assert(!csql::sqlite_bindable<std::optional<int>>);
    static_assert(!csql::sqlite_bindable<const csql::sqlite_int64>); // const 로는 못 받음
    static_assert(!csql::sqlite_bindable<const std::optional<csql::sqlite_int64>>);

    // 복사 금지, 이동만
    static_assert(!std::is_copy_constructible_v<csql::sqlite_row>);
    static_assert(!std::is_copy_assignable_v<csql::sqlite_row>);
    static_assert(std::is_nothrow_move_constructible_v<csql::sqlite_row>);
    static_assert(std::is_move_assignable_v<csql::sqlite_row>);

    // ---------------------------------------------------------------- is_empty
    TEST(SqliteRowEmptyTest, DefaultIsEmpty) {
        const csql::sqlite_row row{};
        EXPECT_TRUE(row.is_empty());
    }

    TEST(SqliteRowEmptyTest, EmptyMapIsEmpty) {
        const csql::sqlite_row row{std::map<std::string, csql::sqlite_values>{}};
        EXPECT_TRUE(row.is_empty());
    }

    TEST_F(SqliteRowTest, WithDataIsNotEmpty) {
        EXPECT_FALSE(row.is_empty());
    }

    TEST_F(SqliteRowTest, MoveKeepsData) {
        csql::sqlite_row moved{std::move(row)};
        auto res = moved.get<csql::sqlite_int64>("id");
        ASSERT_TRUE(res);
        EXPECT_EQ(*res, 42);

        csql::sqlite_row assigned{};
        assigned = std::move(moved);
        auto res2 = assigned.get<csql::sqlite_text>("name");
        ASSERT_TRUE(res2);
        EXPECT_EQ(*res2, "chao");
    }

    // ---------------------------------------------------------------- get
    TEST_F(SqliteRowTest, GetInt64) {
        auto res = row.get<csql::sqlite_int64>("id");
        ASSERT_TRUE(res);
        EXPECT_EQ(*res, 42);
    }

    TEST_F(SqliteRowTest, GetText) {
        auto res = row.get<csql::sqlite_text>("name");
        ASSERT_TRUE(res);
        EXPECT_EQ(*res, "chao");
    }

    TEST_F(SqliteRowTest, GetEmptyTextIsNotNull) {
        auto res = row.get<csql::sqlite_text>("empty_text");
        ASSERT_TRUE(res);
        EXPECT_TRUE(res->empty());
    }

    TEST_F(SqliteRowTest, GetDouble) {
        auto res = row.get<csql::sqlite_double>("score");
        ASSERT_TRUE(res);
        EXPECT_DOUBLE_EQ(*res, 1.5);
    }

    TEST_F(SqliteRowTest, GetBlob) {
        auto res = row.get<csql::sqlite_blob>("payload");
        ASSERT_TRUE(res);
        EXPECT_EQ(*res, (csql::sqlite_blob{0x00, 0x01, 0xFF}));
    }

    TEST_F(SqliteRowTest, GetDoubleFromInt64Converts) {
        auto res = row.get<csql::sqlite_double>("id");
        ASSERT_TRUE(res);
        EXPECT_DOUBLE_EQ(*res, 42.0);
    }

    TEST_F(SqliteRowTest, GetInt64FromDoubleFails) {
        auto res = row.get<csql::sqlite_int64>("score");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    TEST_F(SqliteRowTest, GetTextFromInt64Fails) {
        auto res = row.get<csql::sqlite_text>("id");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    TEST_F(SqliteRowTest, GetBlobFromTextFails) {
        auto res = row.get<csql::sqlite_blob>("name");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    TEST_F(SqliteRowTest, GetMissingKeyFails) {
        auto res = row.get<csql::sqlite_int64>("no_such_column");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found"));
    }

    TEST_F(SqliteRowTest, GetKeyIsCaseSensitive) {
        auto res = row.get<csql::sqlite_int64>("ID");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found"));
    }

    TEST_F(SqliteRowTest, GetValueFromNullFails) {
        auto res = row.get<csql::sqlite_int64>("nick");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("value is null: key=nick"));
    }

    TEST_F(SqliteRowTest, GetDoubleFromNullFails) {
        // int64 → double 변환보다 null 검사가 먼저
        auto res = row.get<csql::sqlite_double>("nick");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("value is null"));
    }

    TEST_F(SqliteRowTest, GetNullFromNull) {
        auto res = row.get<csql::sqlite_null>("nick");
        ASSERT_TRUE(res);
        EXPECT_EQ(*res, nullptr);
    }

    TEST_F(SqliteRowTest, GetNullFromValueFails) {
        auto res = row.get<csql::sqlite_null>("id");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    // ---------------------------------------------------------------- is_null
    TEST_F(SqliteRowTest, IsNullOnNull) {
        auto res = row.is_null("nick");
        EXPECT_TRUE(res);
    }

    TEST_F(SqliteRowTest, IsNullOnValueFails) {
        auto res = row.is_null("id");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("value is not null: key=id"));
    }

    TEST_F(SqliteRowTest, IsNullOnEmptyTextFails) {
        auto res = row.is_null("empty_text");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("value is not null"));
    }

    TEST_F(SqliteRowTest, IsNullMissingKeyFails) {
        auto res = row.is_null("no_such_column");
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found"));
    }

    // ---------------------------------------------------------------- bind (일반 타입)
    TEST_F(SqliteRowTest, BindInt64) {
        csql::sqlite_int64 out = -1;
        auto res = row.bind("id", out);
        ASSERT_TRUE(res);
        EXPECT_EQ(out, 42);
    }

    TEST_F(SqliteRowTest, BindText) {
        csql::sqlite_text out = "old";
        auto res = row.bind("name", out);
        ASSERT_TRUE(res);
        EXPECT_EQ(out, "chao");
    }

    TEST_F(SqliteRowTest, BindBlob) {
        csql::sqlite_blob out;
        auto res = row.bind("payload", out);
        ASSERT_TRUE(res);
        EXPECT_EQ(out, (csql::sqlite_blob{0x00, 0x01, 0xFF}));
    }

    TEST_F(SqliteRowTest, BindDoubleFromInt64Converts) {
        csql::sqlite_double out = 0.0;
        auto res = row.bind("id", out);
        ASSERT_TRUE(res);
        EXPECT_DOUBLE_EQ(out, 42.0);
    }

    TEST_F(SqliteRowTest, BindNullFailsAndKeepsOut) {
        csql::sqlite_int64 out = 7;
        auto res = row.bind("nick", out);
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("value is null: key=nick"));
        EXPECT_EQ(out, 7);
    }

    TEST_F(SqliteRowTest, BindTypeMismatchFailsAndKeepsOut) {
        csql::sqlite_text out = "keep";
        auto res = row.bind("id", out);
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
        EXPECT_EQ(out, "keep");
    }

    TEST_F(SqliteRowTest, BindMissingKeyFailsAndKeepsOut) {
        csql::sqlite_int64 out = 7;
        auto res = row.bind("no_such_column", out);
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found"));
        EXPECT_EQ(out, 7);
    }

    TEST_F(SqliteRowTest, BindSqliteNullType) {
        csql::sqlite_null out{};
        EXPECT_TRUE(row.bind("nick", out));
        auto res = row.bind("id", out);
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    // ---------------------------------------------------------------- bind (optional)
    TEST_F(SqliteRowTest, BindOptionalWithValue) {
        std::optional<csql::sqlite_int64> out;
        auto res = row.bind("id", out);
        ASSERT_TRUE(res);
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(*out, 42);
    }

    TEST_F(SqliteRowTest, BindOptionalTextWithValue) {
        std::optional<csql::sqlite_text> out;
        auto res = row.bind("name", out);
        ASSERT_TRUE(res);
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(*out, "chao");
    }

    TEST_F(SqliteRowTest, BindOptionalNullResets) {
        // 이미 값이 있어도 NULL 이면 비워야 한다
        std::optional<csql::sqlite_text> out = "old";
        auto res = row.bind("nick", out);
        ASSERT_TRUE(res);
        EXPECT_FALSE(out.has_value());
    }

    TEST_F(SqliteRowTest, BindOptionalEmptyTextIsNotNull) {
        std::optional<csql::sqlite_text> out;
        auto res = row.bind("empty_text", out);
        ASSERT_TRUE(res);
        ASSERT_TRUE(out.has_value());
        EXPECT_TRUE(out->empty());
    }

    TEST_F(SqliteRowTest, BindOptionalDoubleFromInt64Converts) {
        std::optional<csql::sqlite_double> out;
        auto res = row.bind("id", out);
        ASSERT_TRUE(res);
        ASSERT_TRUE(out.has_value());
        EXPECT_DOUBLE_EQ(*out, 42.0);
    }

    TEST_F(SqliteRowTest, BindOptionalBlob) {
        std::optional<csql::sqlite_blob> out;
        auto res = row.bind("payload", out);
        ASSERT_TRUE(res);
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(*out, (csql::sqlite_blob{0x00, 0x01, 0xFF}));
    }

    TEST_F(SqliteRowTest, BindOptionalMissingKeyFailsAndKeepsOut) {
        // 컬럼이 없는 것과 NULL 은 다르다: 없으면 실패
        std::optional<csql::sqlite_int64> out = 7;
        auto res = row.bind("no_such_column", out);
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found: key=no_such_column"));
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(*out, 7);
    }

    TEST_F(SqliteRowTest, BindOptionalTypeMismatchFailsAndKeepsOut) {
        std::optional<csql::sqlite_int64> out = 7;
        auto res = row.bind("name", out);
        EXPECT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(*out, 7);
    }

    TEST_F(SqliteRowTest, BindOptionalEmptyMismatchStaysEmpty) {
        std::optional<csql::sqlite_int64> out;
        auto res = row.bind("score", out);
        EXPECT_FALSE(res);
        EXPECT_FALSE(out.has_value());
    }

    // ---------------------------------------------------------------- 여러 컬럼
    TEST_F(SqliteRowTest, BindManyColumns) {
        csql::sqlite_int64 id{};
        csql::sqlite_text name;
        std::optional<csql::sqlite_text> nick = "x";
        std::optional<csql::sqlite_double> score;

        ASSERT_TRUE(row.bind("id", id));
        ASSERT_TRUE(row.bind("name", name));
        ASSERT_TRUE(row.bind("nick", nick));
        ASSERT_TRUE(row.bind("score", score));

        EXPECT_EQ(id, 42);
        EXPECT_EQ(name, "chao");
        EXPECT_FALSE(nick.has_value());
        ASSERT_TRUE(score.has_value());
        EXPECT_DOUBLE_EQ(*score, 1.5);
    }

    TEST_F(SqliteRowTest, GetDoesNotConsumeValue) {
        // 같은 키를 여러 번 읽어도 값이 그대로 남는다 (const 멤버)
        for (int i = 0; i < 3; ++i) {
            auto res = row.get<csql::sqlite_text>("name");
            ASSERT_TRUE(res);
            EXPECT_EQ(*res, "chao");
        }
        csql::sqlite_text a;
        csql::sqlite_text b;
        ASSERT_TRUE(row.bind("name", a));
        ASSERT_TRUE(row.bind("name", b));
        EXPECT_EQ(a, b);
    }
} // namespace
