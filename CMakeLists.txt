cmake_minimum_required(VERSION 4.1)
include(cmake/version.cmake)
project(cpp-sqlite VERSION ${APP_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# win32 utf-8 설정
include(cmake/win32.cmake)

# 라이브러리 불러오기
include(cmake/library.cmake)

# 실행파일 컴파일하기
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "_test\\.cpp$") # 테스트 파일 제외
set(PROGRAM csql)    # 여기서 라이브러리 이름 지정
add_library(${PROGRAM} ${SOURCES})
add_library(${PROGRAM}::${PROGRAM} ALIAS ${PROGRAM}) # alias 지정
target_link_libraries(${PROGRAM} ${LIBRARY})
target_include_directories(${PROGRAM} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# PCH 설정
target_precompile_headers(
        ${PROGRAM}
        PRIVATE
        <ctls/ctls.hpp>
        <nlohmann/json.hpp>
        <sqlite3.h>
)

# 테스트 설정
include(cmake/test.cmake)