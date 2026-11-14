#pragma once
#include <string>
#include <vector>

namespace csql {
    // 테이블 정의 클래스
    class table {
    public:
        explicit table() = default;
        virtual ~table() = default;
        virtual std::string get_table_name() = 0;
        virtual std::vector<std::string> get_migrations() = 0;
    };
} // namespace csql
