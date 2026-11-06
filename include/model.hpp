#pragma once
#include <cstddef>
#include <tuple>
#include <format>
#include <sqlite3.h>

namespace csql::model {
    constexpr int MAX_FIELDS = 20;

    // sqlite 맵핑 struct 는 다음 지정한 것들만 사용하자
    using sqlite_text = std::string;
    using sqlite_int64 = std::int64_t;
    using sqlite_double = double;

    struct sqlite_field {
        template <class U> operator U() const;
    };

    // 입력된 T 타입의 맴버변수 개수를 측정하는 함수
    template <class T, class... A> consteval std::size_t count_fields() {
        if constexpr (requires { T{A{}..., sqlite_field{}}; }) {
            return count_fields<T, A..., sqlite_field>();
        }
        return sizeof...(A);
    }

    // 입력된 T 타입에 데이터를 입력하기 위한 튜플 생성
    // 다른 방법이 없어서 유한개로 작성 됨
    // 추후 코드제너레이터 만들어서 개수를 늘리도록 한다.
    template <class T> constexpr auto to_tuple(T& v) {
        constexpr auto n = count_fields<T>();
        static_assert(1 <= n && n <= MAX_FIELDS, "unsupported field count");

        // clang-format off
    if constexpr (n == 1) {
        auto& [i1] = v;
        return std::tie(i1);
    }
    if constexpr (n == 2) {
        auto& [i1, i2] = v;
        return std::tie(i1, i2);
    }
    if constexpr (n == 3) {
        auto& [i1, i2, i3] = v;
        return std::tie(i1, i2, i3);
    }
    if constexpr (n == 4) {
        auto& [i1, i2, i3, i4] = v;
        return std::tie(i1, i2, i3, i4);
    }
    if constexpr (n == 5) {
        auto& [i1, i2, i3, i4, i5] = v;
        return std::tie(i1, i2, i3, i4, i5);
    }
    if constexpr (n == 6) {
        auto& [i1, i2, i3, i4, i5, i6] = v;
        return std::tie(i1, i2, i3, i4, i5, i6);
    }
    if constexpr (n == 7) {
        auto& [i1, i2, i3, i4, i5, i6, i7] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7);
    }
    if constexpr (n == 8) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8);
    }
    if constexpr (n == 9) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9);
    }
    if constexpr (n == 10) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10);
    }
    if constexpr (n == 11) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11);
    }
    if constexpr (n == 12) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12);
    }
    if constexpr (n == 13) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13);
    }
    if constexpr (n == 14) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14);
    }
    if constexpr (n == 15) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15);
    }
    if constexpr (n == 16) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16);
    }
    if constexpr (n == 17) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16, i17] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16, i17);
    }
    if constexpr (n == 18) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16, i17, i18] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16, i17, i18);
    }
    if constexpr (n == 19) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16, i17, i18, i19] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16, i17, i18, i19);
    }
    if constexpr (n == 20) {
        auto& [i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16, i17, i18, i19, i20] = v;
        return std::tie(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15, i16, i17, i18, i19, i20);
    }
        // clang-format on
    }

    /* ---- 필드 이름 가져오기 ---- */
    template <class T> struct wrapper {
        T value;
    };
    template <class T> extern wrapper<T> address;
    template <auto P> consteval auto signature() {
#ifdef __APPLE__
        return std::string_view(__PRETTY_FUNCTION__);
#elif _WIN32
        return std::string_view(__FUNCSIG__);
#endif
    }

    template <class T, std::size_t I> consteval auto field_ptr() {
        auto fields = to_tuple(address<T>.value);
        return &std::get<I>(fields);
    }
    /* ---- 필드 이름 가져오기 종료 ---- */


    // template <class T> column_map<T> make_column_map(sqlite3_stmt* s) {
    //     static constexpr auto names = refl::field_names<T>();
    //     column_map<T> map;
    //     map.fill(-1);
    //     const int count = sqlite3_column_count(s);
    //     for (int c = 0; c < count; ++c) {
    //         const std::string_view col = sqlite3_column_name(s, c);
    //         for (std::size_t i = 0; i < names.size(); ++i) {
    //             if (names[i] == col) map[i] = c;
    //         }
    //     }
    //     return map;
    // }
    //
    // // 값 읽기는 행마다 (타입이 맞을 때만)
    // template <class T> int from_row(sqlite3_stmt* s, const column_map<T>& map, T& out) {
    //     int mismatched = 0;
    //     auto fields = refl::to_tuple(out);
    //     std::apply([&](auto&... field) {
    //         std::size_t i = 0;
    //         ([&] {
    //             using F = std::remove_cvref_t<decltype(field)>;
    //             const int col = map[i++];
    //             if (col < 0) return;                                   // 이름이 안 맞음
    //             if (!type_matches<F>(sqlite3_column_type(s, col))) {   // 타입이 안 맞음
    //                 ++mismatched;
    //                 return;
    //             }
    //             read_value(s, col, field);
    //         }(), ...);
    //     }, fields);
    //     return mismatched;
    // }

} // namespace csql::model