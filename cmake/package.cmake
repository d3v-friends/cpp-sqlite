# 사용하는 패키지 조회하여 PACKAGE 변수로 내보내기
# get_package(변수명)
function(get_package _OUT_VAR)
    set(_PACKAGE "")
    find_package(unofficial-sqlite3 CONFIG REQUIRED)

    # git 프로젝트에서 라이브러리 불러오기 샘플
    include(FetchContent)
    FetchContent_Declare(
            ctls
            GIT_REPOSITORY https://github.com/d3v-friends/cpp-tools.git
            GIT_TAG v0.0.42
    )
    FetchContent_MakeAvailable(ctls)


    set(_PACKAGE
            ctls::ctls
            unofficial::sqlite3::sqlite3
    )

    set(${_OUT_VAR} ${_PACKAGE} PARENT_SCOPE)
endfunction()
