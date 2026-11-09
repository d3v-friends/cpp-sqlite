// [2026-11-09] client.hpp 테스트 (gtest)
// 옮길 위치: source/client_test.cpp  (cmake/test.cmake 가 source/*_test.cpp 를 수집)
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "types.hpp"
#include "manager.hpp"
#include "client.hpp"
#include "cursor.hpp"
#include "sqlite_row.hpp"

namespace {
    using namespace std::chrono_literals;

    // 테스트마다 별도 DB 파일을 쓰는 fixture
    class ClientTest : public ::testing::Test {
    protected:
        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            _dir = std::filesystem::temp_directory_path() / "csql_client_test" /
                   info->name();
            std::filesystem::create_directories(_dir);
            _path = _dir / "test.db";
            std::filesystem::remove(_path);

            _mgr = std::make_unique<csql::manager>();
            ASSERT_TRUE(_mgr->init(_path, pool_size()));
        }

        void TearDown() override {
            _mgr.reset(); // 커넥션 먼저 닫고
            std::error_code ec;
            std::filesystem::remove_all(_dir, ec); // 테스트용 임시 DB(-wal, -shm 포함) 정리
        }

        virtual std::size_t pool_size() const { return 2; }

        std::unique_ptr<csql::client> client() {
            auto c = _mgr->get_client();
            EXPECT_NE(c, nullptr);
            return c;
        }

        // SELECT 결과 첫 줄을 가져오는 헬퍼
        static csql::sqlite_row first_row(std::unique_ptr<csql::cursor> cur) {
            EXPECT_NE(cur, nullptr);
            if (!cur) return csql::sqlite_row{};
            auto r = cur->next();
            EXPECT_TRUE(r) << r.get_message();
            if (!r) return csql::sqlite_row{};
            return std::move(*r);
        }

        std::filesystem::path _dir;
        std::filesystem::path _path;
        std::unique_ptr<csql::manager> _mgr;
    };

    class SinglePoolClientTest : public ClientTest {
    protected:
        std::size_t pool_size() const override { return 1; }
    };

    // ───────────────────────── exec ─────────────────────────

    TEST_F(ClientTest, ExecEmptySqlFails) {
        auto c = client();
        const auto st = c->exec("");
        EXPECT_FALSE(st);
    }

    TEST_F(ClientTest, ExecCreateAndInsertSucceeds) {
        auto c = client();
        ASSERT_TRUE(c->exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);"));
        ASSERT_TRUE(c->exec("INSERT INTO t (name) VALUES ('a'), ('b');"));

        auto row = first_row(c->query("SELECT COUNT(*) FROM t"));
        auto n = row.get<csql::sqlite_int64>(0);
        ASSERT_TRUE(n) << n.get_message();
        EXPECT_EQ(*n, 2);
    }

    TEST_F(ClientTest, ExecMultipleStatementsRunsAll) {
        auto c = client();
        ASSERT_TRUE(c->exec(
            "CREATE TABLE t (v INTEGER);"
            "INSERT INTO t VALUES (1);"
            "INSERT INTO t VALUES (2);"));

        auto row = first_row(c->query("SELECT SUM(v) FROM t"));
        auto sum = row.get<csql::sqlite_int64>(0);
        ASSERT_TRUE(sum);
        EXPECT_EQ(*sum, 3);
    }

    TEST_F(ClientTest, ExecSyntaxErrorFailsWithMessage) {
        auto c = client();
        const auto st = c->exec("SELEC 1;");
        ASSERT_FALSE(st);
        const std::string msg = st.get_message();
        EXPECT_NE(msg.find("sqlite3_exec failed"), std::string::npos) << msg;
        EXPECT_NE(msg.find("syntax error"), std::string::npos) << msg;
    }

    TEST_F(ClientTest, ExecConstraintViolationFails) {
        auto c = client();
        ASSERT_TRUE(c->exec("CREATE TABLE t (id INTEGER PRIMARY KEY);"));
        ASSERT_TRUE(c->exec("INSERT INTO t VALUES (1);"));
        EXPECT_FALSE(c->exec("INSERT INTO t VALUES (1);")); // UNIQUE 위반
    }

    TEST_F(ClientTest, ExecForeignKeyIsEnforced) {
        // manager::init_db 가 PRAGMA foreign_keys=ON 을 걸었는지 client 로 확인
        auto c = client();
        ASSERT_TRUE(c->exec(
            "CREATE TABLE p (id INTEGER PRIMARY KEY);"
            "CREATE TABLE ch (pid INTEGER REFERENCES p(id));"));
        EXPECT_FALSE(c->exec("INSERT INTO ch VALUES (999);"));
    }

    TEST_F(ClientTest, ExecTransactionRollback) {
        auto c = client();
        ASSERT_TRUE(c->exec("CREATE TABLE t (v INTEGER);"));
        ASSERT_TRUE(c->exec("BEGIN; INSERT INTO t VALUES (1); ROLLBACK;"));

        auto row = first_row(c->query("SELECT COUNT(*) FROM t"));
        auto n = row.get<csql::sqlite_int64>(0);
        ASSERT_TRUE(n);
        EXPECT_EQ(*n, 0);
    }

    // ───────────────────────── query: 실패 경로 ─────────────────────────

    TEST_F(ClientTest, QueryEmptySqlReturnsNull) {
        auto c = client();
        EXPECT_EQ(c->query(""), nullptr);
    }

    TEST_F(ClientTest, QueryPrepareErrorReturnsNull) {
        auto c = client();
        EXPECT_EQ(c->query("SELECT * FROM no_such_table"), nullptr);
        EXPECT_EQ(c->query("SELEC 1"), nullptr);
    }

    TEST_F(ClientTest, QueryTooManyBindArgsReturnsNull) {
        // 자리표시자 1개인데 인자 2개 → 2번째 bind 가 SQLITE_RANGE
        auto c = client();
        EXPECT_EQ(c->query("SELECT ?1", 1, 2), nullptr);
    }

    TEST_F(ClientTest, QueryWithoutArgsWorks) {
        auto c = client();
        auto row = first_row(c->query("SELECT 42"));
        auto v = row.get<csql::sqlite_int64>(0);
        ASSERT_TRUE(v);
        EXPECT_EQ(*v, 42);
    }

    TEST_F(ClientTest, QueryUnboundParamIsNull) {
        // 인자를 덜 넘기면 남은 자리표시자는 NULL
        auto c = client();
        auto row = first_row(c->query("SELECT ?1, ?2", 7));
        EXPECT_FALSE(row.is_null(0));
        EXPECT_TRUE(row.is_null(1));
    }

    // ───────────────────────── query: bind 타입별 ─────────────────────────
    // "SELECT typeof(?1), ?1" 로 sqlite 가 받은 타입과 값을 같이 확인

    TEST_F(ClientTest, BindIntegralTypes) {
        auto c = client();
        const int i32 = -123;
        const std::int64_t i64 = 9'000'000'000LL;
        const unsigned short u16 = 65535;

        {
            auto row = first_row(c->query("SELECT typeof(?1), ?1", i32));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), "integer");
            EXPECT_EQ(*row.get<csql::sqlite_int64>(1), -123);
        }
        {
            auto row = first_row(c->query("SELECT ?1", i64));
            EXPECT_EQ(*row.get<csql::sqlite_int64>(0), 9'000'000'000LL);
        }
        {
            auto row = first_row(c->query("SELECT ?1", u16));
            EXPECT_EQ(*row.get<csql::sqlite_int64>(0), 65535);
        }
        {
            // bool 도 is_integral 이라 0/1 정수로 들어감
            auto row = first_row(c->query("SELECT typeof(?1), ?1", true));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), "integer");
            EXPECT_EQ(*row.get<csql::sqlite_int64>(1), 1);
        }
    }

    TEST_F(ClientTest, BindFloatingTypes) {
        auto c = client();
        {
            auto row = first_row(c->query("SELECT typeof(?1), ?1", 3.5));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), "real");
            EXPECT_DOUBLE_EQ(*row.get<csql::sqlite_double>(1), 3.5);
        }
        {
            auto row = first_row(c->query("SELECT ?1", 0.25f));
            EXPECT_DOUBLE_EQ(*row.get<csql::sqlite_double>(0), 0.25);
        }
    }

    TEST_F(ClientTest, BindTextTypes) {
        auto c = client();
        const std::string s = "hello";
        const std::string_view sv = "world";
        const char* cstr = "cstr";

        {
            auto row = first_row(c->query("SELECT typeof(?1), ?1", s));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), "text");
            EXPECT_EQ(*row.get<csql::sqlite_text>(1), "hello");
        }
        {
            auto row = first_row(c->query("SELECT ?1", sv));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), "world");
        }
        {
            auto row = first_row(c->query("SELECT ?1", cstr));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), "cstr");
        }
        {
            // 문자열 리터럴 (const char[N]) — 끝의 '\0' 이 들어가면 안 됨
            auto row = first_row(c->query("SELECT length(?1), ?1", "lit"));
            EXPECT_EQ(*row.get<csql::sqlite_int64>(0), 3);
            EXPECT_EQ(*row.get<csql::sqlite_text>(1), "lit");
        }
        {
            // 임시 std::string — SQLITE_TRANSIENT 라 query 반환 후에도 값 유지
            auto cur = c->query("SELECT ?1", std::string("temporary"));
            auto row = first_row(std::move(cur));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), "temporary");
        }
        {
            // 중간에 '\0' 이 있어도 size 기준으로 전부 들어감
            const std::string with_nul("a\0b", 3);
            auto row = first_row(c->query("SELECT length(CAST(?1 AS BLOB))", with_nul));
            EXPECT_EQ(*row.get<csql::sqlite_int64>(0), 3);
        }
        {
            const std::string utf8 = "한글 테스트";
            auto row = first_row(c->query("SELECT ?1", utf8));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), utf8);
        }
    }

    TEST_F(ClientTest, BindNullptr) {
        auto c = client();
        auto row = first_row(c->query("SELECT typeof(?1), ?1", nullptr));
        EXPECT_EQ(*row.get<csql::sqlite_text>(0), "null");
        EXPECT_TRUE(row.is_null(1));
    }

    TEST_F(ClientTest, BindBlobTypes) {
        auto c = client();
        const csql::sqlite_blob vec{0x00, 0x01, 0xFE, 0xFF};
        const std::array<std::uint8_t, 3> arr{0xAA, 0xBB, 0xCC};

        {
            auto row = first_row(c->query("SELECT typeof(?1), ?1", vec));
            EXPECT_EQ(*row.get<csql::sqlite_text>(0), "blob");
            EXPECT_EQ(*row.get<csql::sqlite_blob>(1), vec);
        }
        {
            auto row = first_row(c->query("SELECT ?1", arr));
            const csql::sqlite_blob expected{arr.begin(), arr.end()};
            EXPECT_EQ(*row.get<csql::sqlite_blob>(0), expected);
        }
    }

    TEST_F(ClientTest, BindEmptyBlobIsBlobNotNull) {
        // 빈 vector 는 data() 가 nullptr 일 수 있음.
        // sqlite3_bind_blob 에 nullptr 를 주면 NULL 로 바인딩되므로
        // 지금 구현에서는 이 테스트가 실패할 수 있음 (client.inl:68 참고)
        auto c = client();
        const csql::sqlite_blob empty{};
        auto row = first_row(c->query("SELECT typeof(?1)", empty));
        EXPECT_EQ(*row.get<csql::sqlite_text>(0), "blob");
    }

    TEST_F(ClientTest, BindMixedArgsInOrder) {
        auto c = client();
        ASSERT_TRUE(c->exec(
            "CREATE TABLE u (id INTEGER, name TEXT, score REAL, memo TEXT, raw BLOB);"));

        const csql::sqlite_blob raw{1, 2, 3};
        {
            // INSERT 는 next() 를 한 번 불러야 실제로 실행됨 (SQLITE_DONE → 빈 row)
            auto ins = c->query("INSERT INTO u VALUES (?1, ?2, ?3, ?4, ?5)",
                                1, std::string("ciao"), 98.5, nullptr, raw);
            ASSERT_NE(ins, nullptr);
            auto r = ins->next();
            ASSERT_TRUE(r) << r.get_message();
            EXPECT_TRUE(r->is_empty());
        }

        auto row = first_row(c->query("SELECT id, name, score, memo, raw FROM u WHERE id = ?1", 1));
        EXPECT_EQ(*row.get<csql::sqlite_int64>(0), 1);
        EXPECT_EQ(*row.get<csql::sqlite_text>(1), "ciao");
        EXPECT_DOUBLE_EQ(*row.get<csql::sqlite_double>(2), 98.5);
        EXPECT_TRUE(row.is_null(3));
        EXPECT_EQ(*row.get<csql::sqlite_blob>(4), raw);
    }

    TEST_F(ClientTest, QueryReturnsAllRowsThenEnd) {
        auto c = client();
        ASSERT_TRUE(c->exec(
            "CREATE TABLE t (v INTEGER);"
            "INSERT INTO t VALUES (1), (2), (3);"));

        auto cur = c->query("SELECT v FROM t WHERE v >= ?1 ORDER BY v", 2);
        ASSERT_NE(cur, nullptr);

        std::vector<std::int64_t> got;
        while (true) {
            auto r = cur->next();
            ASSERT_TRUE(r) << r.get_message();
            if (r->is_empty()) break;
            got.push_back(*r->get<csql::sqlite_int64>(0));
        }
        EXPECT_EQ(got, (std::vector<std::int64_t>{2, 3}));

        // 끝난 뒤 다시 next() 해도 처음부터 다시 돌지 않음
        auto again = cur->next();
        ASSERT_TRUE(again);
        EXPECT_TRUE(again->is_empty());
    }

    // ───────────────────────── 커넥션 공유 / 수명 ─────────────────────────

    TEST_F(ClientTest, TwoClientsSeeSameDatabase) {
        auto a = client();
        auto b = client();
        ASSERT_TRUE(a->exec("CREATE TABLE t (v TEXT); INSERT INTO t VALUES ('x');"));

        auto row = first_row(b->query("SELECT v FROM t"));
        EXPECT_EQ(*row.get<csql::sqlite_text>(0), "x");
    }

    TEST_F(SinglePoolClientTest, DestroyedClientReturnsConnection) {
        for (int i = 0; i < 5; ++i) {
            auto c = _mgr->get_client();
            ASSERT_NE(c, nullptr) << "loop " << i;
            ASSERT_TRUE(c->exec("SELECT 1;"));
        } // 매번 소멸 → 반납 → 다음 루프에서 재사용
    }

    TEST_F(SinglePoolClientTest, MovedClientKeepsConnection) {
        auto c1 = _mgr->get_client();
        ASSERT_NE(c1, nullptr);
        ASSERT_TRUE(c1->exec("CREATE TABLE t (v INTEGER);"));

        csql::client c2{std::move(*c1)};
        c1.reset(); // 이동된 껍데기 소멸 → 반납되면 안 됨

        auto fut = std::async(std::launch::async, [this] { return _mgr->get_client(); });
        EXPECT_EQ(fut.wait_for(200ms), std::future_status::timeout);

        // 이동받은 쪽은 계속 사용 가능
        EXPECT_TRUE(c2.exec("INSERT INTO t VALUES (1);"));

        // c2 를 없애야 대기 중인 get_client 가 깨어남
        { csql::client dead{std::move(c2)}; }
        auto got = fut.get();
        EXPECT_NE(got, nullptr);
    }

    TEST_F(SinglePoolClientTest, CursorKeepsConnectionAfterClientDestroyed) {
        // cursor 가 shared_ptr<connection> 을 들고 있으므로
        // client 가 먼저 죽어도 cursor 가 살아있는 동안은 반납되지 않아야 함
        auto c = _mgr->get_client();
        ASSERT_NE(c, nullptr);
        ASSERT_TRUE(c->exec("CREATE TABLE t (v INTEGER); INSERT INTO t VALUES (10), (20);"));

        auto cur = c->query("SELECT v FROM t ORDER BY v");
        ASSERT_NE(cur, nullptr);
        c.reset();

        auto fut = std::async(std::launch::async, [this] { return _mgr->get_client(); });
        EXPECT_EQ(fut.wait_for(200ms), std::future_status::timeout);

        // client 가 없어도 cursor 는 계속 읽을 수 있음
        auto r = cur->next();
        ASSERT_TRUE(r) << r.get_message();
        EXPECT_EQ(*r->get<csql::sqlite_int64>(0), 10);

        cur.reset(); // 마지막 소유자 → 여기서 반납
        EXPECT_EQ(fut.wait_for(2s), std::future_status::ready);
        EXPECT_NE(fut.get(), nullptr);
    }
} // namespace
