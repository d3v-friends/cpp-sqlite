#pragma once
#include <cstdint>
#include <string>
#include <sqlite3.h>
#include <memory>

namespace sql {
    // sqlite raii 렙핑
    struct sqlite3_deleter {
        void operator()(sqlite3* p) const noexcept { sqlite3_close_v2(p); }
    };
    using sqlite3_ptr = std::unique_ptr<sqlite3, sqlite3_deleter>;

    struct sqlite3_stmt_deleter {
        void operator()(sqlite3_stmt* p) const noexcept { sqlite3_finalize(p); }
    };
    using sqlite3_stmt_ptr = std::unique_ptr<sqlite3_stmt, sqlite3_stmt_deleter>;

    // sqlite 에서 지정하는 타입만 쓰자
    using sqlite_text = std::string;
    using sqlite_int64 = std::int64_t;
    using sqlite_double = double;

    // 내부용 타입
    using cursor_id = std::uint64_t;

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

    enum class e_db_status : std::uint8_t {
        init,        // 미초기화
        ready,       // 대기중
        in_progress, // 사용중
        terminated,  // 종료됨
    };

    [[nodiscard]] inline std::string to_string(const e_db_status status) {
        switch (status) {
        case e_db_status::init:
            return "초기화";
        case e_db_status::ready:
            return "대기중";
        case e_db_status::in_progress:
            return "사용중";
        case e_db_status::terminated:
            return "종료";
        default:
            return "알수없음";
        }
    }

    // 커넥션 풀로 이용되고 있는 상태
    enum class e_db_pool_status : std::uint8_t {
        idle, // 대기중
        busy, // 사용중
    };

    [[nodiscard]] inline std::string to_string(const e_db_pool_status status) {
        switch (status) {
        case e_db_pool_status::idle:
            return "대기중";
        case e_db_pool_status::busy:
            return "사용중";
        default:
            return "알수없음";
        }
    }
} // namespace sql