cmake_minimum_required(VERSION 4.1)

# 미리 생성한 함수 가져오기
include(cmake/functions.cmake)

# vcpkg.json 파일위치
set(VCPKG_JSON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")

# vcpkg.json 에서 기본정보 가저오기
get_json(VCPKG_NAME "name" FILE "${VCPKG_JSON_PATH}")
get_json(APP_VERSION "version-string" FILE "${VCPKG_JSON_PATH}")
get_json(PACKAGE_NAME "$package-name" FILE "${VCPKG_JSON_PATH}")
get_json(CPP_VERSION "$cpp-version" FILE "${VCPKG_JSON_PATH}")
is_available_cpp_version(${CPP_VERSION})


# 기본폴더 생성하기
mkdir("${CMAKE_CURRENT_SOURCE_DIR}/include/${PACKAGE_NAME}")
mkdir("${CMAKE_CURRENT_SOURCE_DIR}/source/${PACKAGE_NAME}")
mkdir("${CMAKE_CURRENT_SOURCE_DIR}/test/${PACKAGE_NAME}")

# 기본값
set(FILE_EXT "\\.(cpp|c|hpp|h)$")

# 프로젝트 기본설정
project(${VCPKG_NAME} VERSION ${APP_VERSION} LANGUAGES C CXX) # 프로젝트 이름 지정하기
set(CMAKE_CXX_STANDARD ${CPP_VERSION})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 패키지 불러오기. cmake/package.cmake 에 내용추가해주기
get_package(PACKAGES)


# 라이브러리 만들기
get_sources(
        LIB_SOURCES
        "${FILE_EXT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/${PACKAGE_NAME}"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/${PACKAGE_NAME}"
)
make_library(
        LIB
        LIB_NAME ${PACKAGE_NAME}
        PACKAGES ${PACKAGES}
        SOURCES ${LIB_SOURCES}
)
target_precompile_headers(${PACKAGE_NAME} PRIVATE
        # 거의 모든 파일에서 쓰는 표준 헤더 (선택)
        <ctls/ctls.hpp>
)
target_settings(${LIB})


# file generator
generate_version_file(
        NAME ${PACKAGE_NAME}
        TARGETS ${LIB} ${EXE}
)

# test
# CMakeUserPresets.json 에서 관리한다
# "cacheVariables": {
#    "BUILD_TESTS": "ON"
#  }
option(BUILD_TESTS "테스트 빌드" OFF)
if (BUILD_TESTS)
    enable_testing()
    run_test(
            "${VCPKG_NAME}_test"
            "${FILE_EXT}"
            ${LIB}
            "${CMAKE_CURRENT_SOURCE_DIR}/test"
    )
endif ()
