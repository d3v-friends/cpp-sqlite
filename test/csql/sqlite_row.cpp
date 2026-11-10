// [2026-11-09] sqlite_row 단위 테스트
// sqlite_row 는 sqlite 핸들 없이 값(vector<sqlite_values>)만 들고 있으므로
// DB 없이 직접 row 를 만들어 get / is_null / is_empty / move 동작을 확인한다.
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>
#include <ctls/ctls.hpp>

#include "csql/types.hpp"
#include "csql/sqlite_row.hpp"

namespace {
    using csql::sqlite_blob;
    using csql::sqlite_double;
    using csql::sqlite_int64;
    using csql::sqlite_null;
    using csql::sqlite_row;
    using csql::sqlite_text;
    using csql::sqlite_values;

    // 인자 순서대로 컬럼을 채운 row 를 만든다
    template <class... Ts> sqlite_row make_row(Ts&&... vs) {
        std::vector<sqlite_values> v;
        v.reserve(sizeof...(Ts));
        (v.emplace_back(std::forward<Ts>(vs)), ...);
        return sqlite_row{std::move(v)};
    }

    // ----- 컴파일 타임 검사용 concept -----
    template <class T>
    concept row_gettable = requires(const sqlite_row& r) { r.template get<T>(0); };

    template <class T> void take_by_value(T);
    // explicit 기본 생성자면 copy-list-init({}) 가 막힌다
    template <class T>
    concept implicit_default_list_init = requires { take_by_value<T>({}); };
} // namespace

// ---------------------------------------------------------------------------
// 컴파일 타임
// ---------------------------------------------------------------------------
// 복사 금지, move 는 noexcept
static_assert(!std::is_copy_constructible_v<sqlite_row>);
static_assert(!std::is_copy_assignable_v<sqlite_row>);
static_assert(std::is_nothrow_move_constructible_v<sqlite_row>);
static_assert(std::is_nothrow_move_assignable_v<sqlite_row>);
static_assert(std::is_nothrow_destructible_v<sqlite_row>);

// 생성자는 둘 다 explicit
static_assert(std::is_default_constructible_v<sqlite_row>);
static_assert(!implicit_default_list_init<sqlite_row>);
static_assert(std::is_constructible_v<sqlite_row, std::vector<sqlite_values>>);
static_assert(!std::is_convertible_v<std::vector<sqlite_values>, sqlite_row>);

// sqlite_value_type concept: variant 멤버 5개만 허용 (cv/ref 붙으면 다른 타입)
static_assert(csql::sqlite_value_type<sqlite_null>);
static_assert(csql::sqlite_value_type<sqlite_text>);
static_assert(csql::sqlite_value_type<sqlite_int64>);
static_assert(csql::sqlite_value_type<sqlite_double>);
static_assert(csql::sqlite_value_type<sqlite_blob>);
static_assert(!csql::sqlite_value_type<int>);
static_assert(!csql::sqlite_value_type<bool>);
static_assert(!csql::sqlite_value_type<float>);
static_assert(!csql::sqlite_value_type<std::uint64_t>);
static_assert(!csql::sqlite_value_type<std::string_view>);
static_assert(!csql::sqlite_value_type<const char*>);
static_assert(!csql::sqlite_value_type<const sqlite_text>);
static_assert(!csql::sqlite_value_type<sqlite_text&>);
static_assert(!csql::sqlite_value_type<std::vector<char>>);

// is_variant_member: variant 가 아니면 항상 false
static_assert(csql::types::is_variant_member<int, std::variant<int, double>>::value);
static_assert(!csql::types::is_variant_member<long, std::variant<int, double>>::value);
static_assert(!csql::types::is_variant_member<int, std::vector<int>>::value);
static_assert(!csql::types::is_variant_member<int, int>::value);
static_assert(!csql::types::is_variant_member<int, std::variant<>>::value);

// get<T> 는 const row 에서 호출 가능, 허용 안 된 타입은 컴파일 자체가 안 됨
static_assert(row_gettable<sqlite_text>);
static_assert(row_gettable<sqlite_int64>);
static_assert(row_gettable<sqlite_double>);
static_assert(row_gettable<sqlite_blob>);
static_assert(row_gettable<sqlite_null>);
static_assert(!row_gettable<int>);
static_assert(!row_gettable<float>);
static_assert(!row_gettable<std::string_view>);
static_assert(std::same_as<decltype(std::declval<const sqlite_row&>().get<sqlite_text>(0)),
                           ctls::result<sqlite_text>>);
static_assert(std::same_as<decltype(std::declval<const sqlite_row&>().is_null(0)), bool>);
static_assert(std::same_as<decltype(std::declval<const sqlite_row&>().is_empty()), bool>);

// variant 기본값은 첫 번째 타입(null) → 순서가 바뀌면 여기서 잡힘
static_assert(std::holds_alternative<sqlite_null>(sqlite_values{}));

// ---------------------------------------------------------------------------
// 빈 row
// ---------------------------------------------------------------------------
TEST(SqliteRowTest, DefaultRowIsEmpty) {
    const sqlite_row row{};
    EXPECT_TRUE(row.is_empty());
    EXPECT_FALSE(row.is_null(0));

    const auto r = row.get<sqlite_int64>(0);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.get_message(), "out of range");
}

TEST(SqliteRowTest, EmptyVectorRowIsEmpty) {
    const sqlite_row row{std::vector<sqlite_values>{}};
    EXPECT_TRUE(row.is_empty());
    EXPECT_FALSE(row.get<sqlite_text>(0));
}

TEST(SqliteRowTest, RowOfDefaultValuesIsAllNullNotEmpty) {
    // vector(3) → variant 기본값(null) 3개. 컬럼이 있으니 empty 아님
    const sqlite_row row{std::vector<sqlite_values>(3)};
    EXPECT_FALSE(row.is_empty());
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(row.is_null(i)) << "i=" << i;
    }
    EXPECT_FALSE(row.is_null(3));
}

// ---------------------------------------------------------------------------
// 타입별 정상 읽기
// ---------------------------------------------------------------------------
TEST(SqliteRowTest, GetsEachTypeByIndex) {
    const auto row = make_row(sqlite_int64{42}, sqlite_double{3.5}, sqlite_text{"ciao"},
                              sqlite_blob{0x00, 0x7F, 0xFF}, sqlite_null{});
    EXPECT_FALSE(row.is_empty());

    const auto i = row.get<sqlite_int64>(0);
    const auto d = row.get<sqlite_double>(1);
    const auto t = row.get<sqlite_text>(2);
    const auto b = row.get<sqlite_blob>(3);
    ASSERT_TRUE(i);
    ASSERT_TRUE(d);
    ASSERT_TRUE(t);
    ASSERT_TRUE(b);
    EXPECT_EQ(*i, 42);
    EXPECT_DOUBLE_EQ(*d, 3.5);
    EXPECT_EQ(*t, "ciao");
    EXPECT_EQ(*b, (sqlite_blob{0x00, 0x7F, 0xFF}));

    for (std::size_t k = 0; k < 4; ++k) {
        EXPECT_FALSE(row.is_null(k)) << "k=" << k;
    }
    EXPECT_TRUE(row.is_null(4));
}

TEST(SqliteRowTest, Int64Boundaries) {
    constexpr auto max = std::numeric_limits<sqlite_int64>::max();
    constexpr auto min = std::numeric_limits<sqlite_int64>::min();
    const auto row = make_row(max, min, sqlite_int64{0}, sqlite_int64{-1});

    EXPECT_EQ(*row.get<sqlite_int64>(0), max);
    EXPECT_EQ(*row.get<sqlite_int64>(1), min);
    EXPECT_EQ(*row.get<sqlite_int64>(2), 0);
    EXPECT_EQ(*row.get<sqlite_int64>(3), -1);
}

TEST(SqliteRowTest, DoubleSpecialValues) {
    constexpr auto inf = std::numeric_limits<double>::infinity();
    constexpr auto nan = std::numeric_limits<double>::quiet_NaN();
    constexpr auto den = std::numeric_limits<double>::denorm_min();
    const auto row = make_row(inf, -inf, nan, -0.0, den);

    EXPECT_EQ(*row.get<sqlite_double>(0), inf);
    EXPECT_EQ(*row.get<sqlite_double>(1), -inf);
    EXPECT_TRUE(std::isnan(*row.get<sqlite_double>(2)));
    EXPECT_TRUE(std::signbit(*row.get<sqlite_double>(3))); // -0.0 부호 유지
    EXPECT_EQ(*row.get<sqlite_double>(4), den);
}

TEST(SqliteRowTest, TextKeepsEmbeddedNulAndUtf8) {
    using namespace std::string_literals;
    const auto with_nul = "a\0b\0c"s; // 길이 5
    const auto utf8 = sqlite_text{"차오 😀"};
    const auto row = make_row(with_nul, utf8, sqlite_text{});

    const auto t0 = row.get<sqlite_text>(0);
    ASSERT_TRUE(t0);
    EXPECT_EQ(t0->size(), 5u);
    EXPECT_EQ(*t0, with_nul);
    EXPECT_EQ(*row.get<sqlite_text>(1), utf8);

    // 빈 문자열은 NULL 이 아니다
    const auto t2 = row.get<sqlite_text>(2);
    ASSERT_TRUE(t2);
    EXPECT_TRUE(t2->empty());
    EXPECT_FALSE(row.is_null(2));
}

TEST(SqliteRowTest, EmptyBlobIsBlobNotNull) {
    const auto row = make_row(sqlite_blob{});
    const auto b = row.get<sqlite_blob>(0);
    ASSERT_TRUE(b);
    EXPECT_TRUE(b->empty());
    EXPECT_FALSE(row.is_null(0));
}

TEST(SqliteRowTest, LargeBlobRoundTrip) {
    sqlite_blob big(100 * 1024);
    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<std::uint8_t>(i * 31u);
    }
    const auto row = make_row(big);
    const auto b = row.get<sqlite_blob>(0);
    ASSERT_TRUE(b);
    EXPECT_EQ(*b, big);
}

// ---------------------------------------------------------------------------
// int → double 변환
// ---------------------------------------------------------------------------
TEST(SqliteRowTest, IntegerReadableAsDouble) {
    const auto row = make_row(sqlite_int64{7}, sqlite_int64{-3},
                              std::numeric_limits<sqlite_int64>::min());
    EXPECT_DOUBLE_EQ(*row.get<sqlite_double>(0), 7.0);
    EXPECT_DOUBLE_EQ(*row.get<sqlite_double>(1), -3.0);
    EXPECT_DOUBLE_EQ(*row.get<sqlite_double>(2), -9223372036854775808.0); // -2^63 은 정확히 표현됨

    // 변환해서 읽어도 원래 값은 int 그대로
    EXPECT_EQ(*row.get<sqlite_int64>(0), 7);
}

TEST(SqliteRowTest, IntegerToDoubleLosesPrecisionAbove2Pow53) {
    // 주의: 2^53 을 넘는 정수는 double 로 읽으면 값이 바뀐다 (현재 동작 기록)
    constexpr sqlite_int64 big = (sqlite_int64{1} << 53) + 1;
    const auto row = make_row(big);
    const auto d = row.get<sqlite_double>(0);
    ASSERT_TRUE(d);
    EXPECT_EQ(*d, 9007199254740992.0); // 2^53
    EXPECT_NE(static_cast<sqlite_int64>(*d), big);
}

TEST(SqliteRowTest, DoubleNotReadableAsInteger) {
    // 반대 방향(double → int)은 변환하지 않는다
    const auto row = make_row(sqlite_double{2.0});
    const auto r = row.get<sqlite_int64>(0);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.get_message(), "type mismatch");
}

// ---------------------------------------------------------------------------
// 타입 불일치
// ---------------------------------------------------------------------------
TEST(SqliteRowTest, TypeMismatchBetweenAllPairs) {
    const auto row = make_row(sqlite_int64{1}, sqlite_double{1.5}, sqlite_text{"123"},
                              sqlite_blob{0x31});

    struct expect_fail {
        const char* what;
        bool ok;
        std::string msg;
    };
    const auto check = [](const char* what, const auto& r) {
        return expect_fail{what, static_cast<bool>(r), r ? std::string{} : r.get_message()};
    };
    const expect_fail cases[] = {
        check("int as text", row.get<sqlite_text>(0)),
        check("int as blob", row.get<sqlite_blob>(0)),
        check("double as int", row.get<sqlite_int64>(1)),
        check("double as text", row.get<sqlite_text>(1)),
        check("double as blob", row.get<sqlite_blob>(1)),
        check("text as int", row.get<sqlite_int64>(2)), // "123" 이어도 변환 안 함
        check("text as double", row.get<sqlite_double>(2)),
        check("text as blob", row.get<sqlite_blob>(2)),
        check("blob as int", row.get<sqlite_int64>(3)),
        check("blob as double", row.get<sqlite_double>(3)),
        check("blob as text", row.get<sqlite_text>(3)),
    };
    for (const auto& c : cases) {
        EXPECT_FALSE(c.ok) << c.what;
        EXPECT_EQ(c.msg, "type mismatch") << c.what;
    }
}

TEST(SqliteRowTest, GetNullTypeOnNonNullIsTypeMismatch) {
    const auto row = make_row(sqlite_int64{1});
    const auto r = row.get<sqlite_null>(0);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.get_message(), "type mismatch");
}

// ---------------------------------------------------------------------------
// NULL
// ---------------------------------------------------------------------------
TEST(SqliteRowTest, NullFailsForEveryType) {
    const auto row = make_row(sqlite_null{});
    EXPECT_TRUE(row.is_null(0));

    EXPECT_EQ(row.get<sqlite_int64>(0).get_message(), "null");
    EXPECT_EQ(row.get<sqlite_double>(0).get_message(), "null"); // int→double 변환보다 null 검사가 먼저
    EXPECT_EQ(row.get<sqlite_text>(0).get_message(), "null");
    EXPECT_EQ(row.get<sqlite_blob>(0).get_message(), "null");
    // get<sqlite_null> 도 실패한다 → NULL 여부는 is_null 로 확인해야 함
    const auto n = row.get<sqlite_null>(0);
    EXPECT_FALSE(n);
    EXPECT_EQ(n.get_message(), "null");
}

// ---------------------------------------------------------------------------
// 범위 밖
// ---------------------------------------------------------------------------
TEST(SqliteRowTest, OutOfRangeIndex) {
    const auto row = make_row(sqlite_int64{1}, sqlite_null{});

    const std::size_t bad[] = {2, 3, 100, std::numeric_limits<std::size_t>::max()};
    for (const auto i : bad) {
        const auto r = row.get<sqlite_int64>(i);
        EXPECT_FALSE(r) << "i=" << i;
        EXPECT_EQ(r.get_message(), "out of range") << "i=" << i;
        EXPECT_FALSE(row.is_null(i)) << "i=" << i; // 범위 밖은 null 이 아니라 false
    }
}

TEST(SqliteRowTest, ErrorPriorityRangeThenNullThenType) {
    const auto row = make_row(sqlite_null{}, sqlite_text{"x"});
    EXPECT_EQ(row.get<sqlite_blob>(5).get_message(), "out of range");
    EXPECT_EQ(row.get<sqlite_blob>(0).get_message(), "null");
    EXPECT_EQ(row.get<sqlite_blob>(1).get_message(), "type mismatch");
}

// ---------------------------------------------------------------------------
// 값 소유 / 복사
// ---------------------------------------------------------------------------
TEST(SqliteRowTest, GetReturnsIndependentCopy) {
    const auto row = make_row(sqlite_text{"origin"}, sqlite_blob{1, 2, 3});

    auto t = row.get<sqlite_text>(0);
    auto b = row.get<sqlite_blob>(1);
    ASSERT_TRUE(t);
    ASSERT_TRUE(b);
    *t += "-changed";
    b->push_back(4);

    // row 안의 값은 그대로
    EXPECT_EQ(*row.get<sqlite_text>(0), "origin");
    EXPECT_EQ(*row.get<sqlite_blob>(1), (sqlite_blob{1, 2, 3}));
}

TEST(SqliteRowTest, RepeatedGetIsStable) {
    const auto row = make_row(sqlite_int64{9}, sqlite_null{});
    for (int k = 0; k < 5; ++k) {
        EXPECT_EQ(*row.get<sqlite_int64>(0), 9) << "k=" << k;
        EXPECT_EQ(row.get<sqlite_int64>(1).get_message(), "null") << "k=" << k;
        EXPECT_TRUE(row.is_null(1)) << "k=" << k;
    }
}

TEST(SqliteRowTest, RowOwnsValuesAfterSourceDestroyed) {
    sqlite_row row{};
    {
        std::vector<sqlite_values> src;
        src.emplace_back(sqlite_text(1000, 'z'));
        src.emplace_back(sqlite_blob(500, 0xAB));
        row = sqlite_row{std::move(src)};
    } // src 소멸
    EXPECT_EQ(*row.get<sqlite_text>(0), sqlite_text(1000, 'z'));
    EXPECT_EQ(*row.get<sqlite_blob>(1), sqlite_blob(500, 0xAB));
}

TEST(SqliteRowTest, ManyColumnsMixed) {
    constexpr std::size_t n = 1000;
    std::vector<sqlite_values> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        switch (i % 5) {
        case 0: v.emplace_back(sqlite_int64(static_cast<sqlite_int64>(i))); break;
        case 1: v.emplace_back(static_cast<sqlite_double>(i) + 0.5); break;
        case 2: v.emplace_back(std::to_string(i)); break;
        case 3: v.emplace_back(sqlite_blob(i % 7, static_cast<std::uint8_t>(i))); break;
        default: v.emplace_back(sqlite_null{}); break;
        }
    }
    const sqlite_row row{std::move(v)};

    int bad = 0;
    for (std::size_t i = 0; i < n; ++i) {
        bool ok = false;
        switch (i % 5) {
        case 0: ok = *row.get<sqlite_int64>(i) == static_cast<sqlite_int64>(i); break;
        case 1: ok = *row.get<sqlite_double>(i) == static_cast<sqlite_double>(i) + 0.5; break;
        case 2: ok = *row.get<sqlite_text>(i) == std::to_string(i); break;
        case 3: ok = *row.get<sqlite_blob>(i) == sqlite_blob(i % 7, static_cast<std::uint8_t>(i)); break;
        default: ok = row.is_null(i); break;
        }
        if (!ok) ++bad;
    }
    EXPECT_EQ(bad, 0);
    EXPECT_FALSE(row.get<sqlite_int64>(n));
}

// ---------------------------------------------------------------------------
// move
// ---------------------------------------------------------------------------
TEST(SqliteRowTest, MoveConstructKeepsValues) {
    auto a = make_row(sqlite_int64{1}, sqlite_text{"two"}, sqlite_null{});
    const sqlite_row b{std::move(a)};

    EXPECT_FALSE(b.is_empty());
    EXPECT_EQ(*b.get<sqlite_int64>(0), 1);
    EXPECT_EQ(*b.get<sqlite_text>(1), "two");
    EXPECT_TRUE(b.is_null(2));
}

TEST(SqliteRowTest, MoveAssignReplacesValues) {
    auto a = make_row(sqlite_text{"new"});
    auto b = make_row(sqlite_int64{1}, sqlite_int64{2}, sqlite_int64{3});
    b = std::move(a);

    EXPECT_EQ(*b.get<sqlite_text>(0), "new");
    EXPECT_EQ(b.get<sqlite_int64>(1).get_message(), "out of range"); // 예전 컬럼은 사라짐
}

TEST(SqliteRowTest, MoveAssignEmptyRowClearsValues) {
    auto b = make_row(sqlite_int64{1});
    b = sqlite_row{};
    EXPECT_TRUE(b.is_empty());
    EXPECT_EQ(b.get<sqlite_int64>(0).get_message(), "out of range");
}

TEST(SqliteRowTest, MovedFromRowCanBeReassigned) {
    auto a = make_row(sqlite_int64{1});
    const sqlite_row b{std::move(a)};
    a = make_row(sqlite_double{2.5}); // move 된 껍데기에 다시 대입해서 쓸 수 있어야 함
    EXPECT_DOUBLE_EQ(*a.get<sqlite_double>(0), 2.5);
    EXPECT_EQ(*b.get<sqlite_int64>(0), 1);
}

TEST(SqliteRowTest, RowsInVectorSurviveReallocation) {
    // move-only + nothrow move 라서 vector 재할당 때 move 로 옮겨진다
    std::vector<sqlite_row> rows;
    for (sqlite_int64 i = 0; i < 100; ++i) {
        rows.push_back(make_row(i, std::to_string(i)));
    }
    int bad = 0;
    for (sqlite_int64 i = 0; i < 100; ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        if (*r.get<sqlite_int64>(0) != i || *r.get<sqlite_text>(1) != std::to_string(i)) ++bad;
    }
    EXPECT_EQ(bad, 0);
}
