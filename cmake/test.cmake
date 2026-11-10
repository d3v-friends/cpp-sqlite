# 테스트 설정
# _TEST_NAME 테스트 이름
# FILE_REGEX 불러올 파일들 확장자
# PROJECT 이 프로젝트 파일들
# ARGN 테스트폴더
function(run_test _TEST_NAME _FILE_REGEX _PROJECT)
    if (NOT _FILE_REGEX)
        message(FATAL_ERROR "run_test: FILE_REGEX 없음")
    endif ()

    if (NOT _PROJECT)
        message(FATAL_ERROR "run_test: PROJECT 없음")
    endif ()

    # args 받아오기
    if (NOT ARGN)
        message(FATAL_ERROR "run_test: glob 없음. 파일경로 추가 필요")
    endif ()

    get_sources(_TEST_SOURCES "${_FILE_REGEX}" ${ARGN})

    # test 설정
    if (NOT _TEST_SOURCES)
        message(TRACE "run_test: 테스트 파일 없음")
        return()
    endif ()

    # 테스트 활성화
    set(_TEST_PROGRAM "${_TEST_NAME}")
    add_executable(
            ${_TEST_PROGRAM}
            ${_TEST_SOURCES}
    )
    target_settings(${_TEST_PROGRAM})

    # 테스트 페키지 적용
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
            ${_TEST_PROGRAM}
            PRIVATE
            ${_PROJECT}
            GTest::gmock_main
    )
    include(GoogleTest)
    gtest_discover_tests(${_TEST_PROGRAM}
            WORKING_DIRECTORY $<TARGET_FILE_DIR:${_TEST_PROGRAM}>   # asset 상대경로용
            DISCOVERY_TIMEOUT 30
    )
endfunction()




