string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWER)
if (BUILD_TYPE_LOWER STREQUAL "debug")
    # test 설정
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp")
    list(FILTER TEST_SOURCES INCLUDE REGEX "_test\\.cpp$") # 테스트 파일만 포함

    if (TEST_SOURCES)
        set(TEST_PROGRAM "${PROJECT_NAME}_TEST")
        add_executable(
                ${TEST_PROGRAM}
                ${TEST_SOURCES}
        )
        target_include_directories(
                ${TEST_PROGRAM} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/source"
        )
        find_package(GTest CONFIG REQUIRED)
        target_link_libraries(
                ${TEST_PROGRAM}
                PRIVATE
                ${PROJECT_NAME}
                ${LIBRARY}
                GTest::gmock_main
        )
        include(GoogleTest)
        gtest_discover_tests(${TEST_PROGRAM}
                WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_PROGRAM}>   # asset 상대경로용
                DISCOVERY_TIMEOUT 30
        )
        target_precompile_headers(
                ${TEST_PROGRAM}
                PRIVATE
                <ctls/ctls.hpp>
                <nlohmann/json.hpp>
                <sqlite3.h>
        )
    endif ()
endif ()