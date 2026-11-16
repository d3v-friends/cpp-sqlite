#pragma once
#include <string>
#include <vector>

namespace csql {
    // 테이블 정의 클래스
    class table_base {
    public:
        explicit table_base() = default;
        virtual ~table_base() = default;

        // 테이블 이름은 바꾸지 않는다. 이 이름의 테이블이 마이그레이션 뒤에도 실제로 남아 있어야 한다
        virtual std::string get_table_name() = 0;

        // 한번 사용한 쿼리는 수정하지 않는다. 쿼리의 순서를 바꾸지 않는다.
        virtual std::vector<std::string> get_migrations() = 0;
    };
} // namespace csql
