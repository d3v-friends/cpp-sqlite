#pragma once
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <ctls/ctls.hpp>

#include "csql/cursor.hpp"
#include "csql/sqlite_row.hpp"

namespace csql {
    // 컬럼 이름 + 멤버 포인터 한 쌍
    template <class S, class M> struct column {
        std::string_view name;
        M S::* member;
    };

    // 멤버 타입 M 은 &S::x 에서 자동으로 추론됨. bind 가능한 타입만 허용
    template <class S, sqlite_bindable M>
    constexpr column<S, M> col(std::string_view name, M S::* member) {
        return {name, member};
    }

    // S::columns() 를 가진 구조체만 매핑 대상
    template <class S>
    concept row_mappable = requires { S::columns(); };

    /* 바인딩 예제
     * #include <tuple>
     * #include <ctls/ctls.hpp>
     * #include <csql/row_mapper.hpp>
     * #include <csql/types.hpp>
     *
     * struct data {
     *     csql::sqlite_text id;
     *     csql::sqlite_int64_optional user_id;
     *     csql::sqlite_text_optional phone_number;
     *     csql::sqlite_text_optional api_hash;
     *     csql::sqlite_int64_optional api_id;
     *
     *     // 구조체마다 이것만 적으면 됨. 멤버 타입은 &data::x 에서 자동으로 알아냄
     *     static constexpr auto columns() {
     *         return std::tuple{
     *             csql::col("id", &data::id),
     *             csql::col("user_id", &data::user_id),
     *             csql::col("phone_number", &data::phone_number),
     *             csql::col("api_hash", &data::api_hash),
     *             csql::col("api_id", &data::api_id),
     *         };
     *     }
     * };
     *
     * // 기존 static bind(row) 대신
     * auto res = csql::bind_row<data>(row);
     * if (!res) {
     *     ILOG_ERROR("bind failed: error={}", res.extract_failed().message);
     * }
     */

    // row -> S. 컬럼 순서대로 bind 하다가 처음 실패한 곳에서 멈춘다
    template <row_mappable S> ctls::result<S> bind_row(const sqlite_row& row) {
        S out{};
        ctls::result<> res{};

        std::apply(
            [&](const auto&... c) {
                // && 접기: 앞에서 실패하면 뒤는 실행하지 않음
                ((res = row.bind(std::string{c.name}, out.*(c.member)),
                  static_cast<bool>(res)) &&
                 ...);
            },
            S::columns());

        if (!res) {
            return res; // C++20 암시적 이동 -> result_base&& 로 실패 전달
        }
        return out;
    }

    template <row_mappable R> ctls::result<std::vector<R>> bind_all(cursor& cur) {
        std::vector<R> res;

        while (true) {
            auto next = cur.next();
            if (!next) {
                return next;
            }

            if (next->is_empty()) {
                break;
            }

            auto item = bind_row<R>(*next);
            if (!item) {
                return item;
            }

            res.push_back(std::move(*item));
        }

        return res;
    }
} // namespace csql
