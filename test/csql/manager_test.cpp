// csql::manager 테스트
// - db 파일은 실행 폴더(gtest_discover_tests 의 WORKING_DIRECTORY)에 만들고 끝나면
// 지운다.
//   macOS 의 temp_directory_path() 는 TMPDIR 을 무시하는 경우가 있어서 실행 폴더를 쓴다.
// - 결과 검증은 가능하면 manager 가 아닌 별도 raw sqlite3 커넥션으로 한다.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <ctls/ctls.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include "csql/cursor.hpp"
#include "csql/cursor_trx.hpp"
#include "csql/database.hpp"
#include "csql/database_trx.hpp"
#include "csql/manager.hpp"
#include "csql/sqlite_row.hpp"
#include "csql/types.hpp"

namespace {
    using namespace std::chrono_literals;
    using ::testing::HasSubstr;

    // 실패 result 의 메시지를 꺼낸다
    std::string message_of(ctls::result_base& res) {
        if (res) {
            return "<success>";
        }
        return res.extract_failed().message;
    }

    // manager 를 거치지 않는 독립 커넥션. 커밋 여부를 바깥에서 확인하는 용도
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

        // 첫 행 첫 컬럼 정수값. 실패하면 -1
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

    private:
        sqlite3* _db = nullptr;
    };

    // csql 커넥션으로 정수 1개 읽기 (컬럼 이름은 v)
    ctls::result<csql::sqlite_int64>
    query_int(csql::database& db, const std::string& sql) {
        auto cur = db.query(sql);
        if (!cur) {
            return cur.extract_failed();
        }
        auto row = cur->next();
        if (!row) {
            return row.extract_failed();
        }
        return row->get<csql::sqlite_int64>("v");
    }

    // 다른 스레드에서 소멸시키고 2초 안에 끝나는지 본다. 멈추면 false
    bool destroy_without_hang(std::unique_ptr<csql::manager>& mgr) {
        std::atomic<bool> destroyed = false;
        std::thread t{[&] {
            mgr.reset();
            destroyed = true;
        }};
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!destroyed && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        const bool ok = destroyed;
        t.join();
        return ok;
    }

    class ManagerTest : public ::testing::Test {
    protected:
        std::filesystem::path path;
        // 폴더 생성 테스트용 작업 폴더. 테스트마다 따로 쓰고 끝나면 통째로 지운다
        std::filesystem::path dir;

        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            path = std::filesystem::current_path() /
                   (std::string{"csql_manager_"} + info->name() + ".db");
            dir = std::filesystem::current_path() /
                  (std::string{"csql_manager_dir_"} + info->name());
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
            std::filesystem::remove_all(dir, ec);
        }

        // 초기화 + 테이블 생성
        void init(
            csql::manager& mgr,
            const std::size_t max_connections = 2,
            const int wait_busy_milli = 5000) const {
            ASSERT_TRUE(mgr.initialize(path, max_connections, wait_busy_milli));
        }

        static void create_table(csql::manager& mgr, const std::string& sql) {
            auto db = mgr.get_database();
            ASSERT_TRUE(db);
            ASSERT_TRUE((*db)->exec(sql));
        }
    };

    // ---------------------------------------------------------------- initialize

    TEST_F(ManagerTest, InitializeSucceeds) {
        csql::manager mgr;
        EXPECT_TRUE(mgr.initialize(path, 2));
        EXPECT_TRUE(std::filesystem::exists(path));
    }

    TEST_F(ManagerTest, InitializeZeroMaxConnectionsFails) {
        csql::manager mgr;
        auto res = mgr.initialize(path, 0);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("max_connections"));
    }

    TEST_F(ManagerTest, InitializeNegativeBusyTimeoutFails) {
        csql::manager mgr;
        auto res = mgr.initialize(path, 1, -1);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("wait_busy_milli"));
    }

    TEST_F(ManagerTest, InitializeTwiceFails) {
        csql::manager mgr;
        ASSERT_TRUE(mgr.initialize(path, 1));
        auto res = mgr.initialize(path, 1);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("already initialized"));
    }

    // 한 번 실패하면 terminated 가 되어 다시 초기화할 수 없다
    TEST_F(ManagerTest, InitializeAfterFailureCannotRetry) {
        csql::manager mgr;
        ASSERT_FALSE(mgr.initialize(path, 0));
        auto res = mgr.initialize(path, 1);
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("already initialized"));
    }

    // 없는 폴더는 이제 만들어 주므로, 열기 실패는 "db 경로가 폴더" 인 경우로 만든다
    TEST_F(ManagerTest, InitializeOpenFailureFails) {
        ASSERT_TRUE(std::filesystem::create_directories(dir));

        auto mgr = std::make_unique<csql::manager>();
        auto res = mgr->initialize(dir, 3); // 폴더를 db 파일로 열기 -> SQLITE_CANTOPEN
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("failed to open database"));

        auto db = mgr->get_database();
        EXPECT_FALSE(db);

        // 실패 후 소멸자가 커넥션 회수를 기다리며 멈추지 않아야 한다
        EXPECT_TRUE(destroy_without_hang(mgr))
            << "destructor hangs after failed initialize";
    }

    // ---------------------------------------------------------------- initialize: db
    // 폴더 생성

    // 없는 상위 폴더를 여러 단계 한 번에 만든다
    TEST_F(ManagerTest, InitializeCreatesMissingParentDirectories) {
        const auto db_path = dir / "a" / "b" / "c.db";
        ASSERT_FALSE(std::filesystem::exists(dir));

        csql::manager mgr;
        auto res = mgr.initialize(db_path, 2);
        ASSERT_TRUE(res) << message_of(res);

        EXPECT_TRUE(std::filesystem::is_directory(dir / "a" / "b"));
        EXPECT_TRUE(std::filesystem::is_regular_file(db_path));

        // 만들어진 db 를 실제로 쓸 수 있어야 한다
        auto db = mgr.get_database();
        ASSERT_TRUE(db);
        EXPECT_TRUE((*db)->exec("CREATE TABLE t(v INTEGER)"));
    }

    // 폴더가 이미 있으면 create_directories 가 false(에러 없음) 를 돌려준다 -> 실패로
    // 보면 안 된다
    TEST_F(ManagerTest, InitializeWithExistingParentDirectorySucceeds) {
        ASSERT_TRUE(std::filesystem::create_directories(dir / "sub"));
        const auto db_path = dir / "sub" / "x.db";

        csql::manager mgr;
        auto res = mgr.initialize(db_path, 1);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_TRUE(std::filesystem::is_regular_file(db_path));
    }

    // 이미 있는 db 파일을 다시 열면 데이터가 그대로 남아 있어야 한다 (폴더 생성이 기존
    // 파일을 건드리지 않음)
    TEST_F(ManagerTest, InitializeKeepsExistingDatabaseInCreatedDirectory) {
        const auto db_path = dir / "keep" / "x.db";
        {
            csql::manager mgr;
            ASSERT_TRUE(mgr.initialize(db_path, 1));
            auto db = mgr.get_database();
            ASSERT_TRUE(db);
            ASSERT_TRUE((*db)->exec("CREATE TABLE t(v INTEGER)"));
            ASSERT_TRUE((*db)->exec("INSERT INTO t VALUES(?)", 7));
        }

        csql::manager mgr;
        auto res = mgr.initialize(db_path, 1);
        ASSERT_TRUE(res) << message_of(res);

        raw_db probe{db_path};
        EXPECT_EQ(probe.scalar("SELECT v FROM t"), 7);
    }

    // 상위 경로 중간에 일반 파일이 있으면 폴더를 만들 수 없다
    TEST_F(ManagerTest, InitializeDirectoryCreationFailureFails) {
        ASSERT_TRUE(std::filesystem::create_directories(dir));
        const auto blocker = dir / "blocker"; // 폴더가 아니라 파일
        {
            std::ofstream{blocker} << "x";
        }
        ASSERT_TRUE(std::filesystem::is_regular_file(blocker));

        const auto db_path = blocker / "sub" / "x.db";
        auto mgr = std::make_unique<csql::manager>();
        auto res = mgr->initialize(db_path, 3);
        ASSERT_FALSE(res);
        const auto msg = message_of(res);
        EXPECT_THAT(msg, HasSubstr("failed to create db dir"));
        EXPECT_THAT(msg, HasSubstr("blocker")); // 에러에 어떤 경로였는지 들어간다

        // sqlite 를 열기 전에 멈춰야 한다
        EXPECT_FALSE(std::filesystem::exists(db_path));
        EXPECT_TRUE(std::filesystem::is_regular_file(blocker)); // 막고 있던 파일은 그대로

        // terminated 상태: 사용·재초기화 불가, 소멸자는 바로 끝나야 한다
        auto db = mgr->get_database();
        ASSERT_FALSE(db);
        EXPECT_THAT(message_of(db), HasSubstr("not ready"));

        auto again = mgr->initialize(path, 1);
        ASSERT_FALSE(again);
        EXPECT_THAT(message_of(again), HasSubstr("already initialized"));

        EXPECT_TRUE(destroy_without_hang(mgr))
            << "destructor hangs after dir creation failure";
    }

    // 폴더 없이 파일 이름만 주면 parent_path() 가 빈 경로 -> 실행 폴더에 만들어져야 한다
    TEST_F(ManagerTest, InitializeBareFileNameSucceeds) {
        const auto bare = path.filename(); // 실행 폴더 기준 상대경로
        ASSERT_TRUE(bare.parent_path().empty());

        csql::manager mgr;
        auto res = mgr.initialize(bare, 1);
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_TRUE(std::filesystem::is_regular_file(path));
    }

    // 메모리 db 도 parent_path() 가 비어 있다. 폴더 확인 때문에 막히면 안 된다
    TEST_F(ManagerTest, InitializeMemoryDatabaseSucceeds) {
        csql::manager mgr;
        auto res = mgr.initialize(":memory:", 1);
        ASSERT_TRUE(res) << message_of(res);

        auto db = mgr.get_database();
        ASSERT_TRUE(db);
        ASSERT_TRUE((*db)->exec("CREATE TABLE t(v INTEGER)"));
        auto v = query_int(**db, "SELECT count(*) AS v FROM t");
        ASSERT_TRUE(v) << message_of(v);
        EXPECT_EQ(*v, 0);
        EXPECT_FALSE(
            std::filesystem::exists(std::filesystem::current_path() / ":memory:"));
    }

    // max_connections / wait_busy_milli 검사가 폴더 생성보다 먼저다 -> 잘못된 인자면
    // 폴더를 만들지 않는다
    TEST_F(ManagerTest, InitializeInvalidArgsDoNotCreateDirectory) {
        {
            csql::manager mgr;
            ASSERT_FALSE(mgr.initialize(dir / "zero" / "x.db", 0));
        }
        {
            csql::manager mgr;
            ASSERT_FALSE(mgr.initialize(dir / "neg" / "x.db", 1, -1));
        }
        EXPECT_FALSE(std::filesystem::exists(dir));
    }

    TEST_F(ManagerTest, InitializeAppliesPragmasToEveryConnection) {
        csql::manager mgr;
        init(mgr, 3, 1234);

        // 3개를 동시에 빌려서 커넥션마다 확인
        std::vector<std::shared_ptr<csql::database>> dbs;
        for (int i = 0; i < 3; ++i) {
            auto db = mgr.get_database();
            ASSERT_TRUE(db);
            dbs.push_back(std::move(*db));
        }

        for (const auto& db : dbs) {
            // PRAGMA 는 커넥션마다 따로 적용되므로 그 커넥션의 sqlite3* 로 직접 읽는다
            sqlite3* s = db->get_sqlite3();
            ASSERT_NE(s, nullptr);

            auto read = [&](const char* sql) -> std::string {
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(s, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    return "<prepare fail>";
                }
                std::string v;
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                }
                sqlite3_finalize(stmt);
                return v;
            };

            EXPECT_EQ(read("PRAGMA journal_mode"), "wal");
            EXPECT_EQ(read("PRAGMA foreign_keys"), "1");
            EXPECT_EQ(read("PRAGMA synchronous"), "1"); // NORMAL
            EXPECT_EQ(read("PRAGMA busy_timeout"), "1234");
        }
    }

    // ---------------------------------------------------------------- get_database

    TEST_F(ManagerTest, GetDatabaseBeforeInitializeFails) {
        csql::manager mgr;
        auto db = mgr.get_database();
        ASSERT_FALSE(db);
        EXPECT_THAT(message_of(db), HasSubstr("not ready"));
    }

    TEST_F(ManagerTest, GetDatabaseAfterFailedInitializeFails) {
        csql::manager mgr;
        ASSERT_FALSE(mgr.initialize(path, 0));
        EXPECT_FALSE(mgr.get_database());
    }

    TEST_F(ManagerTest, ConnectionsAreDistinct) {
        csql::manager mgr;
        init(mgr, 3);

        std::vector<std::shared_ptr<csql::database>> dbs;
        std::set<sqlite3*> raws;
        for (int i = 0; i < 3; ++i) {
            auto db = mgr.get_database();
            ASSERT_TRUE(db);
            raws.insert((*db)->get_sqlite3());
            dbs.push_back(std::move(*db));
        }
        EXPECT_EQ(raws.size(), 3U);
    }

    TEST_F(ManagerTest, ReleasedConnectionIsReused) {
        csql::manager mgr;
        init(mgr, 1);

        sqlite3* first = nullptr;
        {
            auto db = mgr.get_database();
            ASSERT_TRUE(db);
            first = (*db)->get_sqlite3();
        } // 여기서 반납

        auto db = mgr.get_database();
        ASSERT_TRUE(db);
        EXPECT_EQ((*db)->get_sqlite3(), first);
    }

    // 풀이 비어 있으면 반납될 때까지 기다린다
    TEST_F(ManagerTest, GetDatabaseWaitsUntilRelease) {
        csql::manager mgr;
        init(mgr, 1);

        auto held = mgr.get_database();
        ASSERT_TRUE(held);
        sqlite3* raw = (*held)->get_sqlite3();

        std::atomic<bool> got = false;
        std::atomic<sqlite3*> got_raw = nullptr;
        std::thread waiter{[&] {
            auto db = mgr.get_database();
            if (db) {
                got_raw = (*db)->get_sqlite3();
            }
            got = true;
        }};

        std::this_thread::sleep_for(100ms);
        EXPECT_FALSE(got) << "get_database must block while pool is empty";

        held = ctls::failed("released"); // shared_ptr 해제 -> 반납
        waiter.join();
        EXPECT_TRUE(got);
        EXPECT_EQ(got_raw.load(), raw);
    }

    // 스레드마다 커넥션을 빌려서 동시에 쓴다
    TEST_F(ManagerTest, ConcurrentWriters) {
        csql::manager mgr;
        init(mgr, 3);
        create_table(mgr, "CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER)");

        constexpr int threads = 8;
        constexpr int per_thread = 50;
        std::atomic<int> failures = 0;

        std::vector<std::thread> ts;
        for (int t = 0; t < threads; ++t) {
            ts.emplace_back([&, t] {
                for (int i = 0; i < per_thread; ++i) {
                    auto db = mgr.get_database();
                    if (!db) {
                        ++failures;
                        continue;
                    }
                    if (!(*db)->exec("INSERT INTO t(v) VALUES(?)", t * 1000 + i)) {
                        ++failures;
                    }
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }

        EXPECT_EQ(failures.load(), 0);
        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), threads * per_thread);
    }

    // ---------------------------------------------------------------- 소멸자

    TEST_F(ManagerTest, DestructWithoutInitializeDoesNotHang) {
        std::atomic<bool> destroyed = false;
        std::thread t{[&] {
            {
                csql::manager mgr;
            }
            destroyed = true;
        }};
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!destroyed && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        EXPECT_TRUE(destroyed);
        t.join();
    }

    // 빌려준 database 가 반납될 때까지 소멸자가 기다린다
    TEST_F(ManagerTest, DestructorWaitsForBorrowedDatabase) {
        auto mgr = std::make_unique<csql::manager>();
        init(*mgr, 2);

        auto db = mgr->get_database();
        ASSERT_TRUE(db);
        auto held = std::move(*db);

        std::atomic<bool> destroyed = false;
        std::thread t{[&] {
            mgr.reset();
            destroyed = true;
        }};

        std::this_thread::sleep_for(100ms);
        EXPECT_FALSE(destroyed) << "destructor must wait for borrowed database";

        held.reset(); // 반납 -> 소멸자 진행
        t.join();
        EXPECT_TRUE(destroyed);
    }

    // 소멸 중에는 대기하던 get_database 가 실패로 깨어난다
    TEST_F(ManagerTest, DestructorWakesWaitingGetDatabase) {
        auto mgr = std::make_unique<csql::manager>();
        init(*mgr, 1);

        auto db = mgr->get_database();
        ASSERT_TRUE(db);
        auto held = std::move(*db);

        std::atomic<bool> waiter_done = false;
        std::atomic<bool> waiter_ok = true;
        csql::manager* raw_mgr = mgr.get();
        std::thread waiter{[&] {
            auto res = raw_mgr->get_database(); // 풀이 비어서 대기
            waiter_ok = static_cast<bool>(res);
            waiter_done = true;
        }};
        std::this_thread::sleep_for(50ms); // waiter 가 대기를 시작하도록

        std::atomic<bool> destroyed = false;
        std::thread destroyer{[&] {
            mgr.reset();
            destroyed = true;
        }};

        // 소멸이 시작되면 waiter 는 커넥션 없이 실패로 깨어나야 한다
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!waiter_done && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        EXPECT_TRUE(waiter_done) << "waiter not woken by destructor";
        EXPECT_FALSE(waiter_ok);
        EXPECT_FALSE(destroyed) << "destructor must still wait for held database";

        held.reset();
        waiter.join();
        destroyer.join();
        EXPECT_TRUE(destroyed);
    }
    // ---------------------------------------------------------------- transact

    TEST_F(ManagerTest, TransactBeforeInitializeFails) {
        csql::manager mgr;
        bool called = false;
        auto res = mgr.transact([&](csql::database_trx&) -> ctls::result<> {
            called = true;
            return ctls::ok;
        });
        EXPECT_FALSE(res);
        EXPECT_FALSE(called);
    }

    TEST_F(ManagerTest, TransactCommits) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        auto res = mgr.transact([](csql::database_trx& trx) -> ctls::result<> {
            if (auto r = trx.exec("INSERT INTO t VALUES(?)", 1); !r) {
                return r;
            }
            return trx.exec("INSERT INTO t VALUES(?)", 2);
        });
        ASSERT_TRUE(res) << message_of(res);

        raw_db probe{path}; // 다른 커넥션에서 보여야 커밋된 것
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 2);
    }

    TEST_F(ManagerTest, TransactFnFailureRollsBack) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        auto res = mgr.transact([](csql::database_trx& trx) -> ctls::result<> {
            if (auto r = trx.exec("INSERT INTO t VALUES(1)"); !r) {
                return r;
            }
            return ctls::failed("user fail");
        });
        ASSERT_FALSE(res);
        auto failed = res.extract_failed();
        EXPECT_EQ(failed.message, "user fail");
        EXPECT_EQ(failed.before, nullptr); // rollback 성공 -> 체인 없음

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 0);
    }

    TEST_F(ManagerTest, TransactSqlErrorRollsBack) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        auto res = mgr.transact([](csql::database_trx& trx) -> ctls::result<> {
            if (auto r = trx.exec("INSERT INTO t VALUES(1)"); !r) {
                return r;
            }
            return trx.exec("INSERT INTO no_such_table VALUES(1)");
        });
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("no such table"));

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 0);
    }

    // INSERT OR ROLLBACK 처럼 SQLite 가 스스로 롤백한 경우에도 원래 에러가 맨 앞에 온다
    TEST_F(ManagerTest, TransactAfterSqliteAutoRollbackKeepsOriginalError) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER UNIQUE)");

        auto res = mgr.transact([](csql::database_trx& trx) -> ctls::result<> {
            if (auto r = trx.exec("INSERT INTO t VALUES(1)"); !r) {
                return r;
            }
            return trx.exec("INSERT OR ROLLBACK INTO t VALUES(1)");
        });
        ASSERT_FALSE(res);
        auto failed = res.extract_failed();
        EXPECT_THAT(failed.message, HasSubstr("UNIQUE"));
        EXPECT_EQ(failed.before, nullptr);

        // 다음 트랜잭션이 정상 동작해야 한다
        auto next = mgr.transact([](csql::database_trx& trx) -> ctls::result<> {
            return trx.exec("INSERT INTO t VALUES(2)");
        });
        EXPECT_TRUE(next) << message_of(next);

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 1);
    }

    // 다른 커넥션이 쓰기 락을 잡고 있으면 BEGIN 이 실패하고 fn 은 호출되지 않는다
    TEST_F(ManagerTest, TransactBeginFailureSkipsFn) {
        csql::manager mgr;
        init(mgr, 1, 0); // busy timeout 0 -> 바로 SQLITE_BUSY
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        raw_db other{path};
        ASSERT_EQ(other.exec("BEGIN IMMEDIATE"), SQLITE_OK);

        bool called = false;
        auto res = mgr.transact([&](csql::database_trx&) -> ctls::result<> {
            called = true;
            return ctls::ok;
        });
        EXPECT_FALSE(res);
        EXPECT_FALSE(called);
        EXPECT_THAT(message_of(res), HasSubstr("locked"));

        other.exec("ROLLBACK");
    }

    // COMMIT 이 실패하면 (지연 검사 FK 위반) 에러를 돌려주고 롤백해야 한다
    TEST_F(ManagerTest, TransactCommitFailureRollsBack) {
        csql::manager mgr;
        init(mgr, 1);
        create_table(mgr, "CREATE TABLE p(id INTEGER PRIMARY KEY)");
        create_table(
            mgr,
            "CREATE TABLE c(pid INTEGER REFERENCES p(id) DEFERRABLE INITIALLY DEFERRED)");

        auto res = mgr.transact([](csql::database_trx& trx) -> ctls::result<> {
            return trx.exec("INSERT INTO c VALUES(99)"); // 여기선 성공, COMMIT 때 실패
        });
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("FOREIGN KEY"));

        // 커넥션이 트랜잭션이 열린 채로 반납되면 안 된다
        auto db = mgr.get_database();
        ASSERT_TRUE(db);
        EXPECT_NE(sqlite3_get_autocommit((*db)->get_sqlite3()), 0);

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM c"), 0);
    }

    // 성공이든 실패든 끝나면 커넥션이 반납되고 트랜잭션이 남지 않는다
    TEST_F(ManagerTest, TransactReleasesConnection) {
        csql::manager mgr;
        init(mgr, 1);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        auto ok = mgr.transact([](csql::database_trx& trx) -> ctls::result<> {
            return trx.exec("INSERT INTO t VALUES(1)");
        });
        ASSERT_TRUE(ok) << message_of(ok);

        auto fail = mgr.transact(
            [](csql::database_trx&) -> ctls::result<> { return ctls::failed("x"); });
        ASSERT_FALSE(fail);

        // 풀 크기 1 -> 반납 안 됐으면 여기서 영원히 대기
        std::atomic<bool> got = false;
        std::atomic<int> autocommit = -1;
        std::thread t{[&] {
            auto db = mgr.get_database();
            if (db) {
                autocommit = sqlite3_get_autocommit((*db)->get_sqlite3());
            }
            got = true;
        }};
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!got && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        ASSERT_TRUE(got) << "connection not released after transact";
        t.join();
        EXPECT_NE(autocommit.load(), 0);
    }

    // fn 안에서 trx.query 로 같은 트랜잭션의 미커밋 데이터를 읽는다
    TEST_F(ManagerTest, TransactQueryInsideFn) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        csql::sqlite_int64 seen = -1;
        auto res = mgr.transact([&](csql::database_trx& trx) -> ctls::result<> {
            if (auto r = trx.exec("INSERT INTO t VALUES(7)"); !r) {
                return r;
            }
            auto cur = trx.query("SELECT sum(v) AS v FROM t");
            if (!cur) {
                return cur.extract_failed();
            }
            auto row = cur->next();
            if (!row) {
                return row.extract_failed();
            }
            auto v = row->get<csql::sqlite_int64>("v");
            if (!v) {
                return v.extract_failed();
            }
            seen = *v;
            return ctls::ok;
        });
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_EQ(seen, 7);
    }

    // 풀보다 많은 스레드가 동시에 transact 해도 전부 커밋된다
    TEST_F(ManagerTest, ConcurrentTransact) {
        csql::manager mgr;
        init(mgr, 2);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        constexpr int threads = 4;
        constexpr int per_thread = 25;
        std::atomic<int> failures = 0;

        std::vector<std::thread> ts;
        for (int t = 0; t < threads; ++t) {
            ts.emplace_back([&, t] {
                for (int i = 0; i < per_thread; ++i) {
                    auto res =
                        mgr.transact([&](csql::database_trx& trx) -> ctls::result<> {
                            return trx.exec("INSERT INTO t VALUES(?)", t * 1000 + i);
                        });
                    if (!res) {
                        ++failures;
                    }
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }

        EXPECT_EQ(failures.load(), 0);
        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), threads * per_thread);
    }

    // ---------------------------------------------------------------- transact<T>
    // 주의: 반드시 transact<T> 처럼 T 를 명시해야 한다.
    //       T 없이 result<T> 를 돌려주는 람다를 넘기면 result<> 버전이 선택된다.

    TEST_F(ManagerTest, TransactTBeforeInitializeFails) {
        csql::manager mgr;
        bool called = false;
        auto res = mgr.transact<int>([&](csql::database_trx&) -> ctls::result<int> {
            called = true;
            return 1;
        });
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("not ready"));
        EXPECT_FALSE(called);
    }

    TEST_F(ManagerTest, TransactTReturnsValueAndCommits) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        auto res = mgr.transact<int>([](csql::database_trx& trx) -> ctls::result<int> {
            if (auto r = trx.exec("INSERT INTO t VALUES(?)", 7); !r) {
                return r;
            }
            return 7;
        });
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_EQ(*res, 7);

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 1);
    }

    TEST_F(ManagerTest, TransactTFnFailureRollsBack) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        auto res = mgr.transact<int>([](csql::database_trx& trx) -> ctls::result<int> {
            if (auto r = trx.exec("INSERT INTO t VALUES(1)"); !r) {
                return r;
            }
            return ctls::failed("user fail");
        });
        ASSERT_FALSE(res);
        auto failed = res.extract_failed();
        EXPECT_EQ(failed.message, "user fail");
        EXPECT_EQ(failed.before, nullptr);

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 0);
    }

    TEST_F(ManagerTest, TransactTSqlErrorRollsBack) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        auto res = mgr.transact<int>([](csql::database_trx& trx) -> ctls::result<int> {
            if (auto r = trx.exec("INSERT INTO t VALUES(1)"); !r) {
                return r;
            }
            if (auto r = trx.exec("INSERT INTO no_such_table VALUES(1)"); !r) {
                return r;
            }
            return 1;
        });
        ASSERT_FALSE(res);
        EXPECT_THAT(message_of(res), HasSubstr("no such table"));

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 0);
    }

    TEST_F(ManagerTest, TransactTAfterSqliteAutoRollbackKeepsOriginalError) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER UNIQUE)");

        auto res = mgr.transact<int>([](csql::database_trx& trx) -> ctls::result<int> {
            if (auto r = trx.exec("INSERT INTO t VALUES(1)"); !r) {
                return r;
            }
            if (auto r = trx.exec("INSERT OR ROLLBACK INTO t VALUES(1)"); !r) {
                return r;
            }
            return 1;
        });
        ASSERT_FALSE(res);
        auto failed = res.extract_failed();
        EXPECT_THAT(failed.message, HasSubstr("UNIQUE"));
        EXPECT_EQ(failed.before, nullptr);

        auto next = mgr.transact<int>([](csql::database_trx& trx) -> ctls::result<int> {
            if (auto r = trx.exec("INSERT INTO t VALUES(2)"); !r) {
                return r;
            }
            return 2;
        });
        ASSERT_TRUE(next) << message_of(next);
        EXPECT_EQ(*next, 2);

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 1);
    }

    TEST_F(ManagerTest, TransactTBeginFailureSkipsFn) {
        csql::manager mgr;
        init(mgr, 1, 0);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        raw_db other{path};
        ASSERT_EQ(other.exec("BEGIN IMMEDIATE"), SQLITE_OK);

        bool called = false;
        auto res = mgr.transact<int>([&](csql::database_trx&) -> ctls::result<int> {
            called = true;
            return 1;
        });
        EXPECT_FALSE(res);
        EXPECT_FALSE(called);
        EXPECT_THAT(message_of(res), HasSubstr("locked"));

        other.exec("ROLLBACK");
    }

    TEST_F(ManagerTest, TransactTCommitFailureRollsBack) {
        csql::manager mgr;
        init(mgr, 1);
        create_table(mgr, "CREATE TABLE p(id INTEGER PRIMARY KEY)");
        create_table(
            mgr,
            "CREATE TABLE c(pid INTEGER REFERENCES p(id) DEFERRABLE INITIALLY DEFERRED)");

        auto res = mgr.transact<int>([](csql::database_trx& trx) -> ctls::result<int> {
            if (auto r = trx.exec("INSERT INTO c VALUES(99)"); !r) {
                return r;
            }
            return 99;
        });
        ASSERT_FALSE(res); // fn 은 성공했지만 COMMIT 실패 -> 값 대신 에러
        EXPECT_THAT(message_of(res), HasSubstr("FOREIGN KEY"));

        auto db = mgr.get_database();
        ASSERT_TRUE(db);
        EXPECT_NE(sqlite3_get_autocommit((*db)->get_sqlite3()), 0);

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM c"), 0);
    }

    // move 만 되는 타입도 돌려줄 수 있다
    TEST_F(ManagerTest, TransactTMoveOnlyValue) {
        csql::manager mgr;
        init(mgr);

        auto res = mgr.transact<std::unique_ptr<int>>(
            [](csql::database_trx&) -> ctls::result<std::unique_ptr<int>> {
                return std::make_unique<int>(5);
            });
        ASSERT_TRUE(res) << message_of(res);
        ASSERT_NE(*res, nullptr);
        EXPECT_EQ(**res, 5);
    }

    // fn 안에서 trx.query 로 읽은 값을 그대로 돌려준다
    TEST_F(ManagerTest, TransactTQueryInsideFn) {
        csql::manager mgr;
        init(mgr);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        auto res = mgr.transact<csql::sqlite_int64>(
            [](csql::database_trx& trx) -> ctls::result<csql::sqlite_int64> {
                if (auto r = trx.exec("INSERT INTO t VALUES(3), (4)"); !r) {
                    return r;
                }
                auto cur = trx.query("SELECT sum(v) AS v FROM t");
                if (!cur) {
                    return cur.extract_failed();
                }
                auto row = cur->next();
                if (!row) {
                    return row.extract_failed();
                }
                return row->get<csql::sqlite_int64>("v");
            });
        ASSERT_TRUE(res) << message_of(res);
        EXPECT_EQ(*res, 7);

        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), 2);
    }

    TEST_F(ManagerTest, TransactTReleasesConnection) {
        csql::manager mgr;
        init(mgr, 1);

        auto ok =
            mgr.transact<int>([](csql::database_trx&) -> ctls::result<int> { return 1; });
        ASSERT_TRUE(ok) << message_of(ok);
        auto fail = mgr.transact<int>(
            [](csql::database_trx&) -> ctls::result<int> { return ctls::failed("x"); });
        ASSERT_FALSE(fail);

        std::atomic<bool> got = false;
        std::atomic<int> autocommit = -1;
        std::thread t{[&] {
            auto db = mgr.get_database();
            if (db) {
                autocommit = sqlite3_get_autocommit((*db)->get_sqlite3());
            }
            got = true;
        }};
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!got && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        ASSERT_TRUE(got) << "connection not released after transact<T>";
        t.join();
        EXPECT_NE(autocommit.load(), 0);
    }

    TEST_F(ManagerTest, ConcurrentTransactT) {
        csql::manager mgr;
        init(mgr, 2);
        create_table(mgr, "CREATE TABLE t(v INTEGER)");

        constexpr int threads = 4;
        constexpr int per_thread = 25;
        std::atomic<int> failures = 0;
        std::atomic<std::int64_t> sum = 0;

        std::vector<std::thread> ts;
        for (int t = 0; t < threads; ++t) {
            ts.emplace_back([&, t] {
                for (int i = 0; i < per_thread; ++i) {
                    const int v = t * 1000 + i;
                    auto res = mgr.transact<int>(
                        [&](csql::database_trx& trx) -> ctls::result<int> {
                            if (auto r = trx.exec("INSERT INTO t VALUES(?)", v); !r) {
                                return r;
                            }
                            return v;
                        });
                    if (!res) {
                        ++failures;
                        continue;
                    }
                    sum += *res;
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }

        EXPECT_EQ(failures.load(), 0);
        raw_db probe{path};
        EXPECT_EQ(probe.scalar("SELECT count(*) FROM t"), threads * per_thread);
        EXPECT_EQ(probe.scalar("SELECT sum(v) FROM t"), sum.load());
    }

    // csql 커넥션으로 읽기 (query_int 경로 확인)
    TEST_F(ManagerTest, DatabaseFromPoolCanQuery) {
        csql::manager mgr;
        init(mgr, 1);
        auto db = mgr.get_database();
        ASSERT_TRUE(db);
        auto v = query_int(**db, "SELECT 41 + 1 AS v");
        ASSERT_TRUE(v) << message_of(v);
        EXPECT_EQ(*v, 42);
    }
} // namespace
