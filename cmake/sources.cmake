# 플렛폼별로 분기 나누기
# 파일명 regex 는 \\.(cpp|c|hpp|h)$ 처럼 파일확장자만 입력한다
# get_sources(변수 파일확장자_REGEX 프로젝트기준상대경로 프로젝트기준상대경로...)
function(get_sources _OUT_VAR _FILE_REGEX)
    # regexp 확인
    if (NOT _FILE_REGEX)
        message(FATAL_ERROR "get_sources: 파일 regex 없음")
    endif ()

    # args 받아오기
    if (NOT ARGN)
        message(FATAL_ERROR "get_sources: 파일경로 추가 필요")
    endif ()

    # 경로 normalize 하기
    set(_PATHS "")
    foreach (_ROOT IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH _ROOT BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        string(REGEX REPLACE "/+$" "" _ROOT "${_ROOT}")
        if (NOT IS_DIRECTORY "${_ROOT}")
            message(FATAL_ERROR "get_sources: 폴더 아님: ${_ROOT}")
        endif ()
        list(APPEND _PATHS "${_ROOT}/*")
    endforeach ()

    # suffix 결정
    if (APPLE)                       # macOS, iOS
        set(_SUFFIX apple)
    elseif (WIN32)
        set(_SUFFIX win32)
    elseif (ANDROID)                 # ANDROID 를 LINUX 보다 먼저 검사
        set(_SUFFIX android)
    else ()
        set(_SUFFIX linux)
    endif ()


    # 1) 확장자 맞는 파일만 남기기
    file(GLOB_RECURSE _ALL CONFIGURE_DEPENDS ${_PATHS})
    list(FILTER _ALL INCLUDE REGEX "${_FILE_REGEX}")

    # 2) 다른 플랫폼 파일 빼기 = 공용 + 내 플랫폼
    set(_OTHERS apple win32 android linux)
    list(REMOVE_ITEM _OTHERS ${_SUFFIX})
    list(JOIN _OTHERS "|" _OTHERS_REGEX)  # 예 "win32|android|linux"
    list(FILTER _ALL EXCLUDE REGEX "_(${_OTHERS_REGEX})${_FILE_REGEX}")

    list(REMOVE_DUPLICATES _ALL)
    set(${_OUT_VAR} ${_ALL} PARENT_SCOPE)
endfunction()