// csql/sqlite_row_binder.hpp 테스트
// - col / row_mappable : static_assert 로 컴파일 시기에 확인
// - bind_row           : cursor 없이 std::map 으로 sqlite_row 를 바로 만들어서 확인 (sqlite_row.cpp 와 같은 방식)
// - bind_all           : manager 없이 ":memory:" 커넥션을 database 로 감싸서 확인 (cursor_test.cpp 와 같은 방식)
//                        on_release 에서 sqlite3_close(v2 아님)를 불러서, stmt 가 남은 채 반납되면
//                        rc 가 SQLITE_BUSY(5) 로 남게 한다.
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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
#include "csql/sqlite_row_binder.hpp"
#include "csql/types.hpp"

namespace {
    using ::testing::HasSubstr;
    using ::testing::Not;

    std::string message_of(ctls::result_base& res) {
        if (res) {
            return "<success>";
        }
        return res.extract_failed().message;
    }

    // ---------------------------------------------------------------- 매핑 구조체
    struct user {
        csql::sqlite_int64 id{};
        csql::sqlite_text name;
        csql::sqlite_double_optional score;
        csql::sqlite_blob_optional avatar;

        static constexpr auto columns() {
            return std::tuple{
                csql::col("id", &user::id),
                csql::col("name", &user::name),
                csql::col("score", &user::score),
                csql::col("avatar", &user::avatar),
            };
        }
    };

    // 컬럼 하나만 쓰는 구조체
    struct only_id {
        csql::sqlite_int64 id{};

        static constexpr auto columns() {
            return std::tuple{csql::col("id", &only_id::id)};
        }
    };

    // 지원하는 타입 전부 (필수 + optional)
    struct all_types {
        csql::sqlite_text t;
        csql::sqlite_int64 i{};
        csql::sqlite_double d{};
        csql::sqlite_blob b;
        csql::sqlite_text_optional ot;
        csql::sqlite_int64_optional oi;
        csql::sqlite_double_optional od;
        csql::sqlite_blob_optional ob;

        static constexpr auto columns() {
            return std::tuple{
                csql::col("t", &all_types::t),   csql::col("i", &all_types::i),
                csql::col("d", &all_types::d),   csql::col("b", &all_types::b),
                csql::col("ot", &all_types::ot), csql::col("oi", &all_types::oi),
                csql::col("od", &all_types::od), csql::col("ob", &all_types::ob),
            };
        }
    };

    // 기본값이 있는 멤버 + 매핑하지 않는 멤버
    struct with_defaults {
        csql::sqlite_int64 id{};
        csql::sqlite_int64_optional opt = 99; // NULL 이면 reset 되어야 함
        int not_mapped = 7;                   // columns() 에 없으니 그대로 유지

        static constexpr auto columns() {
            return std::tuple{
                csql::col("id", &with_defaults::id),
                csql::col("opt", &with_defaults::opt),
            };
        }
    };

    // 컬럼이 하나도 없는 매핑
    struct no_columns {
        int x = 3;
        static constexpr auto columns() {
            return std::tuple<>{};
        }
    };

    // columns() 가 없는 구조체
    struct not_mappable {
        csql::sqlite_int64 id{};
    };

    // bind 불가능한 멤버 타입을 가진 구조체 (col 이 거부해야 함)
    struct bad_members {
        int i{};
        float f{};
        const char* p{};
        std::optional<csql::sqlite_null> on;
        csql::sqlite_int64 ok{};
    };

    // ---------------------------------------------------------------- col (컴파일 시기)
    template <class S, class M>
    concept col_callable = requires(M S::* m) { csql::col<S>("x", m); };

    // 멤버 타입이 &S::x 에서 추론되고, column<S, M> 이 나온다
    static_assert(std::is_same_v<decltype(csql::col("id", &user::id)),
                                 csql::column<user, csql::sqlite_int64>>);
    static_assert(std::is_same_v<decltype(csql::col("score", &user::score)),
                                 csql::column<user, csql::sqlite_double_optional>>);
    static_assert(std::is_same_v<decltype(csql::col("avatar", &user::avatar)),
                                 csql::column<user, csql::sqlite_blob_optional>>);

    // constexpr 로 만들어지고 name / member 가 그대로 들어간다
    constexpr auto k_id_col = csql::col("id", &user::id);
    static_assert(k_id_col.name == std::string_view{"id"});
    static_assert(k_id_col.member == &user::id);

    // bindable 타입만 허용
    static_assert(col_callable<bad_members, csql::sqlite_int64>);
    static_assert(!col_callable<bad_members, int>);
    static_assert(!col_callable<bad_members, float>);
    static_assert(!col_callable<bad_members, const char*>);
    static_assert(!col_callable<bad_members, std::optional<csql::sqlite_null>>);

    // ---------------------------------------------------------------- row_mappable (컴파일 시기)
    static_assert(csql::row_mappable<user>);
    static_assert(csql::row_mappable<only_id>);
    static_assert(csql::row_mappable<all_types>);
    static_assert(csql::row_mappable<no_columns>);
    static_assert(!csql::row_mappable<not_mappable>);
    static_assert(!csql::row_mappable<int>);

    // bind_row / bind_all 은 row_mappable 이 아니면 호출 자체가 안 된다
    template <class S>
    concept bind_row_callable = requires(const csql::sqlite_row& r) { csql::bind_row<S>(r); };
    template <class S>
    concept bind_all_callable = requires(csql::cursor& c) { csql::bind_all<S>(c); };

    static_assert(bind_row_callable<user>);
    static_assert(!bind_row_callable<not_mappable>);
    static_assert(bind_all_callable<user>);
    static_assert(!bind_all_callable<not_mappable>);

    // 반환 타입
    static_assert(std::is_same_v<decltype(csql::bind_row<user>(std::declval<const csql::sqlite_row&>())),
                                 ctls::result<user>>);
    static_assert(std::is_same_v<decltype(csql::bind_all<user>(std::declval<csql::cursor&>())),
                                 ctls::result<std::vector<user>>>);

    // ---------------------------------------------------------------- col (실행)
    TEST(ColTest, StoresNameAndMember) {
        const auto c = csql::col("name", &user::name);
        EXPECT_EQ(c.name, "name");

        user u;
        u.*(c.member) = "chao";
        EXPECT_EQ(u.name, "chao");
    }

    TEST(ColTest, ColumnsTupleKeepsDeclarationOrder) {
        constexpr auto cols = user::columns();
        static_assert(std::tuple_size_v<decltype(cols)> == 4);
        EXPECT_EQ(std::get<0>(cols).name, "id");
        EXPECT_EQ(std::get<1>(cols).name, "name");
        EXPECT_EQ(std::get<2>(cols).name, "score");
        EXPECT_EQ(std::get<3>(cols).name, "avatar");
    }

    // ---------------------------------------------------------------- bind_row
    csql::sqlite_row make_row(std::map<std::string, csql::sqlite_values> data) {
        return csql::sqlite_row{std::move(data)};
    }

    csql::sqlite_row make_user_row() {
        std::map<std::string, csql::sqlite_values> data;
        data.emplace("id", csql::sqlite_int64{1});
        data.emplace("name", csql::sqlite_text{"alice"});
        data.emplace("score", csql::sqlite_double{9.5});
        data.emplace("avatar", csql::sqlite_blob{0x01, 0x02});
        return make_row(std::move(data));
    }

    TEST(BindRowTest, BindsAllColumns) {
        const auto row = make_user_row();
        auto res = csql::bind_row<user>(row);
        ASSERT_TRUE(res) << message_of(res);

        EXPECT_EQ(res->id, 1);
        EXPECT_EQ(res->name, "alice");
        ASSERT_TRUE(res->score.has_value());
        EXPECT_DOUBLE_EQ(*res->score, 9.5);
        ASSERT_TRUE(res->avatar.has_value());
        EXPECT_EQ(*res->avatar, (csql::sqlite_blob{0x01, 0x02}));
    }

    TEST(BindRowTest, BindsEveryValueType) {
        std::map<std::string, csql::sqlite_values> data;
        data.emplace("t", csql::sqlite_text{"text"});
        data.emplace("i", csql::sqlite_int64{-5});
        data.emplace("d", csql::sqlite_double{0.25});
        data.emplace("b", csql::sqlite_blob{0x00, 0xFF});
        data.emplace("ot", csql::sqlite_text{"opt"});
        data.emplace("oi", csql::sqlite_int64{INT64_MAX});
        data.emplace("od", csql::sqlite_double{-1.5});
        data.emplace("ob", csql::sqlite_blob{});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<all_types>(row);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_EQ(res->t, "text");
        EXPECT_EQ(res->i, -5);
        EXPECT_DOUBLE_EQ(res->d, 0.25);
        EXPECT_EQ(res->b, (csql::sqlite_blob{0x00, 0xFF}));
        EXPECT_EQ(res->ot, csql::sqlite_text_optional{"opt"});
        EXPECT_EQ(res->oi, csql::sqlite_int64_optional{INT64_MAX});
        ASSERT_TRUE(res->od.has_value());
        EXPECT_DOUBLE_EQ(*res->od, -1.5);
        ASSERT_TRUE(res->ob.has_value()); // 빈 blob 은 NULL 이 아니다
        EXPECT_TRUE(res->ob->empty());
    }

    TEST(BindRowTest, NullBecomesEmptyOptional) {
        std::map<std::string, csql::sqlite_values> data;
        data.emplace("t", csql::sqlite_text{});
        data.emplace("i", csql::sqlite_int64{0});
        data.emplace("d", csql::sqlite_double{0});
        data.emplace("b", csql::sqlite_blob{});
        data.emplace("ot", csql::sqlite_null{});
        data.emplace("oi", csql::sqlite_null{});
        data.emplace("od", csql::sqlite_null{});
        data.emplace("ob", csql::sqlite_null{});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<all_types>(row);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_FALSE(res->ot.has_value());
        EXPECT_FALSE(res->oi.has_value());
        EXPECT_FALSE(res->od.has_value());
        EXPECT_FALSE(res->ob.has_value());
    }

    TEST(BindRowTest, IntegerIsConvertedToDouble) {
        std::map<std::string, csql::sqlite_values> data;
        data.emplace("id", csql::sqlite_int64{1});
        data.emplace("name", csql::sqlite_text{"a"});
        data.emplace("score", csql::sqlite_int64{7}); // REAL 컬럼에 정수가 들어온 경우
        data.emplace("avatar", csql::sqlite_null{});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<user>(row);
        ASSERT_TRUE(res) << message_of(res);
        ASSERT_TRUE(res->score.has_value());
        EXPECT_DOUBLE_EQ(*res->score, 7.0);
    }

    TEST(BindRowTest, NullResetsDefaultOptionalAndKeepsUnmappedMembers) {
        std::map<std::string, csql::sqlite_values> data;
        data.emplace("id", csql::sqlite_int64{5});
        data.emplace("opt", csql::sqlite_null{});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<with_defaults>(row);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_EQ(res->id, 5);
        EXPECT_FALSE(res->opt.has_value()); // 기본값 99 가 NULL 로 덮인다
        EXPECT_EQ(res->not_mapped, 7);      // 매핑 안 한 멤버는 S{} 값 그대로
    }

    TEST(BindRowTest, ExtraKeysAreIgnored) {
        std::map<std::string, csql::sqlite_values> data;
        data.emplace("id", csql::sqlite_int64{3});
        data.emplace("unused", csql::sqlite_text{"x"});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<only_id>(row);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_EQ(res->id, 3);
    }

    TEST(BindRowTest, EmptyColumnsAlwaysSucceeds) {
        const csql::sqlite_row row{};
        auto res = csql::bind_row<no_columns>(row);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_EQ(res->x, 3);
    }

    TEST(BindRowTest, EmptyRowFails) {
        const csql::sqlite_row row{};
        auto res = csql::bind_row<only_id>(row);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found"));
    }

    TEST(BindRowTest, MissingRequiredColumnFails) {
        auto data = std::map<std::string, csql::sqlite_values>{};
        data.emplace("id", csql::sqlite_int64{1});
        data.emplace("score", csql::sqlite_null{});
        data.emplace("avatar", csql::sqlite_null{});
        const auto row = make_row(std::move(data)); // name 없음

        auto res = csql::bind_row<user>(row);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found"));
    }

    TEST(BindRowTest, MissingOptionalColumnFails) {
        // optional 이라도 컬럼 자체가 없으면 실패 (NULL 과 다름)
        auto data = std::map<std::string, csql::sqlite_values>{};
        data.emplace("id", csql::sqlite_int64{1});
        data.emplace("name", csql::sqlite_text{"a"});
        data.emplace("score", csql::sqlite_null{});
        const auto row = make_row(std::move(data)); // avatar 없음

        auto res = csql::bind_row<user>(row);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found: key=avatar"));
    }

    TEST(BindRowTest, NullInRequiredColumnFails) {
        auto data = std::map<std::string, csql::sqlite_values>{};
        data.emplace("id", csql::sqlite_int64{1});
        data.emplace("name", csql::sqlite_null{});
        data.emplace("score", csql::sqlite_null{});
        data.emplace("avatar", csql::sqlite_null{});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<user>(row);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("value is null: key=name"));
    }

    TEST(BindRowTest, TypeMismatchFails) {
        auto data = std::map<std::string, csql::sqlite_values>{};
        data.emplace("id", csql::sqlite_text{"not-a-number"});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<only_id>(row);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    TEST(BindRowTest, TypeMismatchInOptionalFails) {
        // optional 이어도 NULL 이 아닌 다른 타입이면 실패
        auto data = std::map<std::string, csql::sqlite_values>{};
        data.emplace("id", csql::sqlite_int64{1});
        data.emplace("name", csql::sqlite_text{"a"});
        data.emplace("score", csql::sqlite_text{"high"});
        data.emplace("avatar", csql::sqlite_null{});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<user>(row);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    TEST(BindRowTest, DoubleIsNotConvertedToInteger) {
        // int64 <- double 변환은 없다 (반대 방향만 있음)
        auto data = std::map<std::string, csql::sqlite_values>{};
        data.emplace("id", csql::sqlite_double{1.0});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<only_id>(row);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    TEST(BindRowTest, KeyIsCaseSensitive) {
        auto data = std::map<std::string, csql::sqlite_values>{};
        data.emplace("ID", csql::sqlite_int64{1});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<only_id>(row);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found"));
    }

    TEST(BindRowTest, StopsAtFirstFailingColumn) {
        // name(2번째) 는 NULL, avatar(4번째) 는 없음 -> name 에서 멈춘다
        auto data = std::map<std::string, csql::sqlite_values>{};
        data.emplace("id", csql::sqlite_int64{1});
        data.emplace("name", csql::sqlite_null{});
        data.emplace("score", csql::sqlite_null{});
        const auto row = make_row(std::move(data));

        auto res = csql::bind_row<user>(row);
        ASSERT_FALSE(res);
        const auto msg = message_of(res);
        EXPECT_THAT(msg, HasSubstr("key=name"));
        EXPECT_THAT(msg, Not(HasSubstr("key=avatar")));
    }

    TEST(BindRowTest, RowIsNotModified) {
        const auto row = make_user_row();
        auto first = csql::bind_row<user>(row);
        auto second = csql::bind_row<user>(row);
        ASSERT_TRUE(first) << message_of(first);
        ASSERT_TRUE(second) << message_of(second);
        EXPECT_EQ(first->name, second->name);
        EXPECT_EQ(first->avatar, second->avatar);
    }

    // ---------------------------------------------------------------- bind_all
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

    class BindAllTest : public ::testing::Test {
    protected:
        std::shared_ptr<release_probe> probe = std::make_shared<release_probe>();
        std::shared_ptr<csql::database> db;

        void SetUp() override {
            db = open_memory(probe);
            exec_ok("CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, score REAL, "
                    "avatar BLOB)");
        }

        void TearDown() override {
            db.reset();
            EXPECT_EQ(probe->count, 1);
            EXPECT_EQ(probe->close_rc, SQLITE_OK);
        }

        void exec_ok(const std::string& sql) {
            auto res = db->exec(sql);
            ASSERT_TRUE(res) << sql << " : " << message_of(res);
        }

        csql::cursor query_ok(const std::string& sql) {
            auto cur = db->query(sql);
            if (!cur) {
                ADD_FAILURE() << sql << " : " << message_of(cur);
                return csql::cursor{nullptr, nullptr};
            }
            return std::move(*cur);
        }
    };

    // 성공
    TEST_F(BindAllTest, BindsAllRowsInOrder) {
        exec_ok("INSERT INTO users VALUES"
                "(1, 'alice', 9.5, X'0102'),"
                "(2, 'bob', NULL, NULL),"
                "(3, 'carol', 7, X'')");
        auto cur = query_ok("SELECT id, name, score, avatar FROM users ORDER BY id");

        auto res = csql::bind_all<user>(cur);
        ASSERT_TRUE(res) << message_of(res);
        ASSERT_EQ(res->size(), 3U);

        const auto& a = (*res)[0];
        EXPECT_EQ(a.id, 1);
        EXPECT_EQ(a.name, "alice");
        ASSERT_TRUE(a.score.has_value());
        EXPECT_DOUBLE_EQ(*a.score, 9.5);
        ASSERT_TRUE(a.avatar.has_value());
        EXPECT_EQ(*a.avatar, (csql::sqlite_blob{1, 2}));

        // NULL 은 빈 optional
        const auto& b = (*res)[1];
        EXPECT_EQ(b.id, 2);
        EXPECT_EQ(b.name, "bob");
        EXPECT_FALSE(b.score.has_value());
        EXPECT_FALSE(b.avatar.has_value());

        // REAL 컬럼에 정수 7 이 들어가도 double 로 변환, X'' 는 NULL 이 아닌 빈 blob
        const auto& c = (*res)[2];
        EXPECT_EQ(c.id, 3);
        ASSERT_TRUE(c.score.has_value());
        EXPECT_DOUBLE_EQ(*c.score, 7.0);
        ASSERT_TRUE(c.avatar.has_value());
        EXPECT_TRUE(c.avatar->empty());

        // 다 읽었으므로 cursor 는 종료 상태
        EXPECT_TRUE(cur.is_end());
    }

    TEST_F(BindAllTest, EmptyResultReturnsEmptyVector) {
        auto cur = query_ok("SELECT id, name, score, avatar FROM users");
        auto res = csql::bind_all<user>(cur);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_TRUE(res->empty());
        EXPECT_TRUE(cur.is_end());
    }

    TEST_F(BindAllTest, ExtraColumnsAreIgnored) {
        auto cur = query_ok("SELECT 1 AS id, 'x' AS unused UNION ALL SELECT 2, 'y'");
        auto res = csql::bind_all<only_id>(cur);
        ASSERT_TRUE(res) << message_of(res);
        ASSERT_EQ(res->size(), 2U);
        EXPECT_EQ((*res)[0].id, 1);
        EXPECT_EQ((*res)[1].id, 2);
    }

    TEST_F(BindAllTest, ManyRows) {
        auto cur = query_ok("WITH RECURSIVE s(id) AS (SELECT 1 UNION ALL SELECT id + 1 FROM s "
                            "WHERE id < 1000) SELECT id FROM s");
        auto res = csql::bind_all<only_id>(cur);
        ASSERT_TRUE(res) << message_of(res);
        ASSERT_EQ(res->size(), 1000U);
        for (std::size_t i = 0; i < res->size(); ++i) {
            ASSERT_EQ((*res)[i].id, static_cast<std::int64_t>(i + 1));
        }
    }

    TEST_F(BindAllTest, PartiallyConsumedCursorBindsRemainingRows) {
        auto cur = query_ok("SELECT 1 AS id UNION ALL SELECT 2 UNION ALL SELECT 3");
        auto first = cur.next();
        ASSERT_TRUE(first) << message_of(first);
        ASSERT_FALSE(first->is_empty());

        auto res = csql::bind_all<only_id>(cur);
        ASSERT_TRUE(res) << message_of(res);
        ASSERT_EQ(res->size(), 2U);
        EXPECT_EQ((*res)[0].id, 2);
        EXPECT_EQ((*res)[1].id, 3);
    }

    TEST_F(BindAllTest, ExhaustedCursorReturnsEmptyVector) {
        auto cur = query_ok("SELECT 1 AS id");
        auto once = csql::bind_all<only_id>(cur);
        ASSERT_TRUE(once) << message_of(once);
        EXPECT_EQ(once->size(), 1U);

        // 종료된 cursor 를 다시 넘기면 실패가 아니라 빈 결과
        auto twice = csql::bind_all<only_id>(cur);
        ASSERT_TRUE(twice) << message_of(twice);
        EXPECT_TRUE(twice->empty());
    }

    TEST_F(BindAllTest, ReleasesMutexAfterReturn) {
        auto cur = query_ok("SELECT 1 AS id UNION ALL SELECT 2");
        auto res = csql::bind_all<only_id>(cur);
        ASSERT_TRUE(res) << message_of(res);
        ASSERT_TRUE(db->get_mutex().try_lock());
        db->get_mutex().unlock();
    }

    // bind 실패
    TEST_F(BindAllTest, NullInRequiredColumnFails) {
        exec_ok("INSERT INTO users VALUES(1, 'alice', 1.0, NULL), (2, NULL, 2.0, NULL), "
                "(3, 'carol', 3.0, NULL)");
        auto cur = query_ok("SELECT id, name, score, avatar FROM users ORDER BY id");

        auto res = csql::bind_all<user>(cur);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("value is null: key=name"));

        // bind 실패는 cursor 실패가 아니다. 실패한 행 다음부터 계속 읽을 수 있다
        EXPECT_FALSE(cur.is_end());
        auto rest = csql::bind_all<user>(cur);
        ASSERT_TRUE(rest) << message_of(rest);
        ASSERT_EQ(rest->size(), 1U);
        EXPECT_EQ((*rest)[0].id, 3);
    }

    TEST_F(BindAllTest, MissingColumnFails) {
        exec_ok("INSERT INTO users VALUES(1, 'alice', NULL, NULL)");
        auto cur = query_ok("SELECT id, name, score FROM users"); // avatar 없음

        auto res = csql::bind_all<user>(cur);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("key not found: key=avatar"));
    }

    TEST_F(BindAllTest, TypeMismatchFails) {
        auto cur = query_ok("SELECT 'not-a-number' AS id");
        auto res = csql::bind_all<only_id>(cur);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("type mismatch"));
    }

    TEST_F(BindAllTest, FirstFailingColumnWins) {
        // id·name 둘 다 틀렸을 때 앞 컬럼(id) 에서 멈춘다
        auto cur = query_ok("SELECT 'x' AS id, NULL AS name, NULL AS score, NULL AS avatar");
        auto res = csql::bind_all<user>(cur);
        ASSERT_FALSE(res);
        const auto msg = message_of(res);
        EXPECT_THAT(msg, HasSubstr("type mismatch"));
        EXPECT_THAT(msg, Not(HasSubstr("key=name")));
    }

    // cursor 실패
    TEST_F(BindAllTest, StepFailurePropagates) {
        // abs(INT64_MIN) 은 step 단계에서 integer overflow
        auto cur = query_ok("SELECT abs(-9223372036854775807 - 1) AS id");
        auto res = csql::bind_all<only_id>(cur);
        ASSERT_FALSE(res);
        const auto msg = message_of(res);
        EXPECT_THAT(msg, HasSubstr("failed step stmt"));
        EXPECT_THAT(msg, HasSubstr("integer overflow"));
        EXPECT_TRUE(cur.is_end());
    }

    TEST_F(BindAllTest, StepFailureAfterRowsDiscardsPartialResult) {
        auto cur = query_ok("SELECT 1 AS id UNION ALL SELECT abs(-9223372036854775807 - 1)");
        auto res = csql::bind_all<only_id>(cur);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("integer overflow"));

        // cursor 실패는 sticky: 다시 불러도 같은 실패
        auto again = csql::bind_all<only_id>(cur);
        ASSERT_FALSE(again);
        EXPECT_THAT(message_of(again), HasSubstr("integer overflow"));
    }

    TEST_F(BindAllTest, DuplicateColumnFails) {
        auto cur = query_ok("SELECT 1 AS id, 2 AS id");
        auto res = csql::bind_all<only_id>(cur);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("duplicate column name: id"));
    }

    TEST_F(BindAllTest, NullCursorFails) {
        csql::cursor cur{nullptr, nullptr};
        auto res = csql::bind_all<only_id>(cur);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("cursor is nullptr"));
    }
} // namespace
