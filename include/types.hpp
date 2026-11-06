#pragma once
#include <cstdint>
#include <string>
#include <sqlite3.h>
#include <memory>

namespace csql {
    struct sqlite3_deleter {
        void operator()(sqlite3* p) const noexcept { sqlite3_close_v2(p); }
    };
    using sqlite3_ptr = std::unique_ptr<sqlite3, sqlite3_deleter>;

    struct sqlite3_stmt_deleter {
        void operator()(sqlite3_stmt* p) const noexcept { sqlite3_finalize(p); }
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
} // namespace sql