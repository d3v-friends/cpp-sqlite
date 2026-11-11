#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <sqlite3.h>

namespace csql {
    // sqlite 맵핑 struct 는 다음 지정한 것들만 사용하자
    using sqlite_text = std::string;
    using sqlite_int64 = std::int64_t;
    using sqlite_double = double;
    using sqlite_blob = std::vector<std::uint8_t>;
    using sqlite_null = std::nullptr_t;

    // 여기서 variant 순서도 중요함. 순서대로 매칭을 시작하게 된다.
    using sqlite_values =
        std::variant<sqlite_null, sqlite_text, sqlite_int64, sqlite_double, sqlite_blob>;

    struct sqlite3_deleter {
        void operator()(sqlite3* p) const noexcept {
            sqlite3_close_v2(p);
        }
    };
    using sqlite3_ptr = std::unique_ptr<sqlite3, sqlite3_deleter>;

    struct sqlite3_stmt_deleter {
        void operator()(sqlite3_stmt* p) const noexcept {
            sqlite3_finalize(p);
        }
    };
    using sqlite3_stmt_ptr = std::unique_ptr<sqlite3_stmt, sqlite3_stmt_deleter>;

    // enum 정의
    enum class e_manager_status : std::uint8_t {
        init,        // 초기화
        connecting,  // 연결중
        ready,       // 사용가능
        busy,        // 풀 꽉참
        terminating, // 종료중
        terminated,  // 종료됨
    };

    [[nodiscard]] inline std::string to_string(const e_manager_status status) {
        using enum e_manager_status;
        switch (status) {
        case init:
            return "초기화";
        case connecting:
            return "연결중";
        case ready:
            return "대기중";
        case busy:
            return "풀 꽉참";
        case terminating:
            return "종료중";
        case terminated:
            return "종료";
        default:
            return "알수없음";
        }
    }
} // namespace csql

namespace csql::types {
    /*
     * #include <type_traits>
     * template <class T> struct is_int : std::false_type {};    // 기본 버전
     * template <> struct is_int<int> : std::true_type {};       // T가 int일 때 전용
     * is_int<double>::value;   // false (기본 버전)
     * is_int<int>::value;      // true  (전용 버전)
     */

    /* std::false_type
     * strict bool
     */
    template <class T, class V> struct is_variant_member : std::false_type {};

    // 부분 특수화: V가 std::variant<...>면 이 버전을 쓰고, 안쪽 타입을 Ts로 꺼낸다.
    // std::is_same<T, Ts>... 는 Ts의 타입마다 is_same을 하나씩 만든다.
    // disjunction은 타입으로 쓰는 || 이고, 처음으로 참인 조건을 상속한다.
    // 전부 거짓이면 마지막 조건을 상속한다.
    // is_same이 참이면 std::true_type을 상속하므로 ::value가 true가 된다.
    // 컴파일 시기에 결정
    template <class T, class... Ts> // ① Ts: 타입 여러 개를 받는 묶음 선언
    struct is_variant_member<T, std::variant<Ts...>> // ② V가 variant면 안쪽 타입을 Ts로
                                                     // 꺼냄
        : std::disjunction<std::is_same<T, Ts>...> {
    }; // ③ 타입마다 is_same을 하나씩 만듦, 제일먼저 참이 된것만 적용됨
} // namespace csql::types
