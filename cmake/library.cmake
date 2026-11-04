# 라이브러리 불러와서 LIBRARY 변수로 출력한다

# ctls 라이브러리 불러오기
include(FetchContent)
FetchContent_Declare(
        ctls
        GIT_REPOSITORY https://github.com/d3v-friends/cpp-tools.git
        GIT_TAG v0.0.38
)
FetchContent_MakeAvailable(ctls)

# vcpkg 라이브러리 불러오기
find_package(unofficial-sqlite3 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED) # json 관련 유틸

# 변수 지정
set(LIBRARY
        ctls::ctls
        nlohmann_json::nlohmann_json
        unofficial::sqlite3::sqlite3
)

