// csql::manager::migrate 테스트
// - db 파일은 실행 폴더(gtest_discover_tests 의 WORKING_DIRECTORY)에 만들고 끝나면 지운다.
// - 결과 검증은 manager 가 아닌 별도 raw sqlite3 커넥션으로 한다.
// - table_base 규칙(이름 안 바꿈, 그 이름의 테이블이 마지막까지 남음, 쿼리 수정·순서변경 금지)을
//   지키는 테이블만 쓴다.
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ctls/ctls.hpp>
#include <sqlite3.h>

#include "csql/manager.hpp"
#include "csql/table_base.hpp"
#include "csql/types.hpp"

namespace {
    using ::testing::HasSubstr;

    std::string message_of(ctls::result_base& res) {
        if (res) {
            return "<success>";
        }
        return res.extract_failed().message;
    }

    // manager 를 거치지 않는 독립 커넥션
    class raw_db {
    public:
        explicit raw_db(const std::filesystem::path& path) {
            sqlite3_open_v2(
                path.string().c_str(), &_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                nullptr);
        }
        ~raw_db() {
            sqlite3_close_v2(_db);
        }
        raw_db(const raw_db&) = delete;
        raw_db& operator=(const raw_db&) = delete;

        int exec(const std::string& sql) const {
            return sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, nullptr);
        }

        // 첫 행 첫 컬럼 정수값. 실패하거나 행이 없으면 -1
        [[nodiscard]] std::int64_t scalar(const std::string& sql) const {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return -1;
            }
            std::int64_t v = -1;
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                v = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
            return v;
        }

        // csql 에 저장된 버전. 행이 없으면 -1
        [[nodiscard]] std::int64_t version_of(const std::string& table) const {
            return scalar(std::string{"SELECT version FROM csql WHERE table_name = '"} + table + "'");
        }

        [[nodiscard]] bool has_table(const std::string& table) const {
            return scalar(
                       std::string{"SELECT count(*) FROM sqlite_master "
                                   "WHERE type = 'table' AND name = '"} +
                       table + "'") > 0;
        }

        [[nodiscard]] bool has_column(const std::string& table, const std::string& column) const {
            return scalar(
                       std::string{"SELECT count(*) FROM pragma_table_info('"} + table +
                       "') WHERE name = '" + column + "'") > 0;
        }

    private:
        sqlite3* _db = nullptr;
    };

    // 이름과 마이그레이션 목록을 바깥에서 정하는 테이블
    class test_table : public csql::table_base {
    public:
        test_table(std::string name, std::vector<std::string> migrations)
            : _name{std::move(name)}, _migrations{std::move(migrations)} {}

        std::string get_table_name() override {
            return _name;
        }
        std::vector<std::string> get_migrations() override {
            return _migrations;
        }

    private:
        std::string _name;
        std::vector<std::string> _migrations;
    };

    test_table users_v1() {
        return {"users", {"CREATE TABLE users(id INTEGER PRIMARY KEY)"}};
    }

    test_table users_v2() {
        return {"users",
                {"CREATE TABLE users(id INTEGER PRIMARY KEY)",
                 "ALTER TABLE users ADD COLUMN name TEXT"}};
    }

    ctls::result<> migrate(csql::manager& mgr, std::initializer_list<csql::table_base*> list) {
        std::vector<csql::table_base*> tables{list};
        return mgr.migrate(std::span<csql::table_base* const>{tables});
    }

    class MigrateTest : public ::testing::Test {
    protected:
        std::filesystem::path path;

        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            path = std::filesystem::current_path() /
                   (std::string{"csql_migrate_"} + info->name() + ".db");
            remove_files();
        }

        void TearDown() override {
            remove_files();
        }

        void remove_files() const {
            std::error_code ec;
            for (const char* suffix : {"", "-wal", "-shm", "-journal"}) {
                std::filesystem::remove(path.string() + suffix, ec);
            }
        }

        void init(csql::manager& mgr, const std::size_t max_connections = 2) const {
            ASSERT_TRUE(mgr.initialize(path, max_connections));
        }

        // 새 manager 로 한 번 실행 (앱 재시작 흉내)
        void migrate_once(std::initializer_list<csql::table_base*> list) const {
            csql::manager mgr;
            init(mgr);
            auto res = migrate(mgr, list);
            ASSERT_TRUE(res) << message_of(res);
        }
    };

    // ---------------------------------------------------------------- 기본

    TEST_F(MigrateTest, MigrateBeforeInitializeFails) {
        csql::manager mgr;
        auto users = users_v1();
        EXPECT_FALSE(migrate(mgr, {&users}));
    }

    TEST_F(MigrateTest, EmptySpanCreatesCsqlTable) {
        migrate_once({});

        const raw_db raw{path};
        EXPECT_TRUE(raw.has_table(std::string{csql::CSQL_TABLE_NAME}));
        EXPECT_EQ(raw.version_of(std::string{csql::CSQL_TABLE_NAME}), 1);
    }

    TEST_F(MigrateTest, FreshDbAppliesAllMigrations) {
        auto users = users_v2();
        migrate_once({&users});

        const raw_db raw{path};
        EXPECT_TRUE(raw.has_table("users"));
        EXPECT_TRUE(raw.has_column("users", "name"));
        EXPECT_EQ(raw.version_of("users"), 2);
        EXPECT_EQ(raw.version_of("csql"), 1);
    }

    TEST_F(MigrateTest, TableWithNoMigrationsHasNoVersionRow) {
        test_table empty{"empty", {}};
        migrate_once({&empty});

        const raw_db raw{path};
        EXPECT_EQ(raw.version_of("empty"), -1);
        EXPECT_EQ(raw.scalar("SELECT count(*) FROM csql"), 1); // csql 자기 행만
    }

    TEST_F(MigrateTest, OneVersionRowPerTable) {
        auto users = users_v2();
        test_table posts{"posts", {"CREATE TABLE posts(id INTEGER PRIMARY KEY)"}};
        migrate_once({&users, &posts});

        const raw_db raw{path};
        EXPECT_EQ(raw.scalar("SELECT count(*) FROM csql"), 3);
        EXPECT_EQ(raw.version_of("posts"), 1);
    }

    // ---------------------------------------------------------------- 재실행·추가

    TEST_F(MigrateTest, RerunIsNoop) {
        test_table seeded{
            "seeded",
            {"CREATE TABLE seeded(id INTEGER PRIMARY KEY)",
             "INSERT INTO seeded(id) VALUES(1)"}};

        migrate_once({&seeded});
        migrate_once({&seeded}); // 다시 돌면 CREATE 가 실패하거나 INSERT 가 중복된다

        const raw_db raw{path};
        EXPECT_EQ(raw.scalar("SELECT count(*) FROM seeded"), 1);
        EXPECT_EQ(raw.version_of("seeded"), 2);
    }

    TEST_F(MigrateTest, RerunOnSameManagerIsNoop) {
        csql::manager mgr;
        init(mgr);
        auto users = users_v2();

        auto first = migrate(mgr, {&users});
        ASSERT_TRUE(first) << message_of(first);
        auto second = migrate(mgr, {&users});
        ASSERT_TRUE(second) << message_of(second);

        EXPECT_EQ(raw_db{path}.version_of("users"), 2);
    }

    TEST_F(MigrateTest, AddedMigrationAppliesOnlyNewOnes) {
        test_table v1{
            "items",
            {"CREATE TABLE items(id INTEGER PRIMARY KEY)", "INSERT INTO items(id) VALUES(1)"}};
        migrate_once({&v1});

        test_table v2{
            "items",
            {"CREATE TABLE items(id INTEGER PRIMARY KEY)", "INSERT INTO items(id) VALUES(1)",
             "ALTER TABLE items ADD COLUMN name TEXT", "INSERT INTO items(id) VALUES(2)"}};
        migrate_once({&v2});

        const raw_db raw{path};
        EXPECT_TRUE(raw.has_column("items", "name"));
        EXPECT_EQ(raw.scalar("SELECT count(*) FROM items"), 2);
        EXPECT_EQ(raw.version_of("items"), 4);
    }

    TEST_F(MigrateTest, AddedTableAppliesWithoutTouchingOthers) {
        auto users = users_v2();
        migrate_once({&users});

        test_table posts{"posts", {"CREATE TABLE posts(id INTEGER PRIMARY KEY)"}};
        migrate_once({&users, &posts});

        const raw_db raw{path};
        EXPECT_EQ(raw.version_of("users"), 2);
        EXPECT_EQ(raw.version_of("posts"), 1);
    }

    TEST_F(MigrateTest, DataSurvivesRerun) {
        auto users = users_v2();
        migrate_once({&users});
        ASSERT_EQ(raw_db{path}.exec("INSERT INTO users(id, name) VALUES(7, 'ciao')"), SQLITE_OK);

        migrate_once({&users});

        EXPECT_EQ(raw_db{path}.scalar("SELECT id FROM users WHERE name = 'ciao'"), 7);
    }

    TEST_F(MigrateTest, ResumesFromStoredVersion) {
        // 예전 버전 앱이 만든 DB: users 1단계까지 적용
        {
            const raw_db raw{path};
            ASSERT_EQ(
                raw.exec("CREATE TABLE csql(table_name TEXT PRIMARY KEY, "
                         "version INTEGER NOT NULL DEFAULT 0) WITHOUT ROWID"),
                SQLITE_OK);
            ASSERT_EQ(raw.exec("INSERT INTO csql VALUES('csql', 1), ('users', 1)"), SQLITE_OK);
            ASSERT_EQ(raw.exec("CREATE TABLE users(id INTEGER PRIMARY KEY)"), SQLITE_OK);
        }

        auto users = users_v2();
        migrate_once({&users});

        const raw_db raw{path};
        EXPECT_TRUE(raw.has_column("users", "name"));
        EXPECT_EQ(raw.version_of("users"), 2);
    }

    TEST_F(MigrateTest, RenameKeepingFinalNameReruns) {
        test_table people{
            "people",
            {"CREATE TABLE people_old(id INTEGER PRIMARY KEY)",
             "ALTER TABLE people_old RENAME TO people"}};

        migrate_once({&people});
        migrate_once({&people});

        const raw_db raw{path};
        EXPECT_TRUE(raw.has_table("people"));
        EXPECT_FALSE(raw.has_table("people_old"));
        EXPECT_EQ(raw.version_of("people"), 2);
    }

    // ---------------------------------------------------------------- 순서

    TEST_F(MigrateTest, TablesRunInSpanOrder) {
        test_table parent{"parent", {"CREATE TABLE parent(id INTEGER PRIMARY KEY)"}};
        test_table child{
            "child",
            {"CREATE TABLE child(id INTEGER PRIMARY KEY, "
             "parent_id INTEGER NOT NULL REFERENCES parent(id))",
             "INSERT INTO parent(id) VALUES(1)",
             "INSERT INTO child(id, parent_id) VALUES(1, 1)"}};

        migrate_once({&parent, &child});

        const raw_db raw{path};
        EXPECT_EQ(raw.scalar("SELECT count(*) FROM child"), 1);
    }

    TEST_F(MigrateTest, MigrationsRunInVectorOrder) {
        test_table seq{
            "seq",
            {"CREATE TABLE seq(id INTEGER PRIMARY KEY AUTOINCREMENT, v INTEGER)",
             "INSERT INTO seq(v) VALUES(10)", "INSERT INTO seq(v) VALUES(20)",
             "INSERT INTO seq(v) VALUES(30)"}};
        migrate_once({&seq});

        const raw_db raw{path};
        EXPECT_EQ(raw.scalar("SELECT group_concat(v, '') FROM (SELECT v FROM seq ORDER BY id)"),
                  102030);
    }

    // ---------------------------------------------------------------- 실패

    TEST_F(MigrateTest, FailedMigrationRollsBackThatTable) {
        test_table broken{
            "broken",
            {"CREATE TABLE broken(id INTEGER PRIMARY KEY)", "THIS IS NOT SQL"}};

        csql::manager mgr;
        init(mgr);
        auto res = migrate(mgr, {&broken});
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("THIS IS NOT SQL"));

        const raw_db raw{path};
        EXPECT_FALSE(raw.has_table("broken")); // 1단계도 같이 롤백
        EXPECT_EQ(raw.version_of("broken"), -1);
    }

    TEST_F(MigrateTest, FailureStopsLaterTables) {
        test_table broken{"broken", {"THIS IS NOT SQL"}};
        auto users = users_v1();

        csql::manager mgr;
        init(mgr);
        EXPECT_FALSE(migrate(mgr, {&broken, &users}));

        EXPECT_FALSE(raw_db{path}.has_table("users"));
    }

    TEST_F(MigrateTest, FailureKeepsEarlierTables) {
        // 테이블마다 트랜잭션이 따로라서 앞 테이블은 커밋된 채 남는다
        auto users = users_v2();
        test_table broken{"broken", {"THIS IS NOT SQL"}};

        csql::manager mgr;
        init(mgr);
        EXPECT_FALSE(migrate(mgr, {&users, &broken}));

        EXPECT_EQ(raw_db{path}.version_of("users"), 2);
    }

    TEST_F(MigrateTest, RetryAfterFixResumesFromFailedStep) {
        test_table broken{
            "items",
            {"CREATE TABLE items(id INTEGER PRIMARY KEY)", "INSERT INTO items(id) VALUES(1)"}};
        migrate_once({&broken});

        // 3단계가 실패하는 배포
        test_table bad{
            "items",
            {"CREATE TABLE items(id INTEGER PRIMARY KEY)", "INSERT INTO items(id) VALUES(1)",
             "ALTER TABLE items ADD COLUMN name TEXT", "INSERT INTO nowhere VALUES(1)"}};
        {
            csql::manager mgr;
            init(mgr);
            EXPECT_FALSE(migrate(mgr, {&bad}));
        }
        {
            const raw_db raw{path};
            EXPECT_EQ(raw.version_of("items"), 2);
            EXPECT_FALSE(raw.has_column("items", "name")); // 3단계도 롤백
        }

        // 고친 배포
        test_table fixed{
            "items",
            {"CREATE TABLE items(id INTEGER PRIMARY KEY)", "INSERT INTO items(id) VALUES(1)",
             "ALTER TABLE items ADD COLUMN name TEXT", "INSERT INTO items(id) VALUES(2)"}};
        migrate_once({&fixed});

        const raw_db raw{path};
        EXPECT_TRUE(raw.has_column("items", "name"));
        EXPECT_EQ(raw.scalar("SELECT count(*) FROM items"), 2);
        EXPECT_EQ(raw.version_of("items"), 4);
    }

    TEST_F(MigrateTest, MultipleStatementsInOneMigrationFails) {
        // 마이그레이션 항목 하나 = SQL 문장 하나
        test_table multi{
            "multi",
            {"CREATE TABLE multi(id INTEGER PRIMARY KEY); INSERT INTO multi VALUES(1)"}};

        csql::manager mgr;
        init(mgr);
        auto res = migrate(mgr, {&multi});
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("multiple statements not supported"));
        EXPECT_FALSE(raw_db{path}.has_table("multi"));
    }

    TEST_F(MigrateTest, EmptyMigrationStringFails) {
        test_table blank{"blank", {""}};

        csql::manager mgr;
        init(mgr);
        auto res = migrate(mgr, {&blank});
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("sql is empty"));
    }

    TEST_F(MigrateTest, MigrateReleasesConnection) {
        csql::manager mgr;
        init(mgr, 1);
        auto users = users_v2();
        test_table broken{"broken", {"THIS IS NOT SQL"}};

        auto ok = migrate(mgr, {&users});
        ASSERT_TRUE(ok) << message_of(ok);
        EXPECT_FALSE(migrate(mgr, {&broken}));

        // 커넥션이 1개라서 반납이 안 됐으면 여기서 멈춘다
        auto db = mgr.get_database();
        EXPECT_TRUE(db) << message_of(db);
    }

    // ---------------------------------------------------------------- 동시 실행

    TEST_F(MigrateTest, ConcurrentMigrateOnSameManager) {
        csql::manager mgr;
        init(mgr, 4);

        constexpr int THREADS = 4;
        std::vector<int> ok(THREADS, 0);
        std::vector<std::string> errors(THREADS);
        {
            std::vector<std::jthread> threads;
            for (int i = 0; i < THREADS; ++i) {
                threads.emplace_back([&, i] {
                    test_table seeded{
                        "seeded",
                        {"CREATE TABLE seeded(id INTEGER PRIMARY KEY)",
                         "INSERT INTO seeded(id) VALUES(1)"}};
                    auto res = migrate(mgr, {&seeded});
                    ok[i] = res ? 1 : 0;
                    errors[i] = message_of(res);
                });
            }
        }

        for (int i = 0; i < THREADS; ++i) {
            EXPECT_EQ(ok[i], 1) << "thread " << i << ": " << errors[i];
        }
        const raw_db raw{path};
        EXPECT_EQ(raw.scalar("SELECT count(*) FROM seeded"), 1);
        EXPECT_EQ(raw.version_of("seeded"), 2);
    }

    TEST_F(MigrateTest, ConcurrentMigrateOnSeparateManagers) {
        // 프로세스 여러 개가 같은 파일에 동시에 migrate 하는 경우 흉내
        // 새 파일에 initialize 를 동시에 하면 WAL 전환에서 database is locked 가 날 수 있어서
        // (migrate 와 별개 문제) 파일은 먼저 한 번 만들어 둔다
        {
            csql::manager mgr;
            init(mgr, 1);
        }

        constexpr int THREADS = 3;
        std::vector<int> ok(THREADS, 0);
        std::vector<std::string> errors(THREADS);
        {
            std::vector<std::jthread> threads;
            for (int i = 0; i < THREADS; ++i) {
                threads.emplace_back([&, i] {
                    csql::manager mgr;
                    if (auto init_res = mgr.initialize(path, 1); !init_res) {
                        errors[i] = message_of(init_res);
                        return;
                    }
                    auto users = users_v2();
                    auto res = migrate(mgr, {&users});
                    ok[i] = res ? 1 : 0;
                    errors[i] = message_of(res);
                });
            }
        }

        for (int i = 0; i < THREADS; ++i) {
            EXPECT_EQ(ok[i], 1) << "thread " << i << ": " << errors[i];
        }
        EXPECT_EQ(raw_db{path}.version_of("users"), 2);
    }
} // namespace
