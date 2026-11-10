# 폴더가 없으면 만든다 (중간 폴더까지 한 번에)
#
# mkdir(<절대경로>...)
#   예) mkdir("${CMAKE_CURRENT_SOURCE_DIR}/include/${PROGRAM_NAME}")
function(mkdir)
    if (NOT ARGN)
        message(FATAL_ERROR "mkdir: 경로 없음")
    endif ()

    foreach (_DIR IN LISTS ARGN)
        cmake_path(IS_ABSOLUTE _DIR _IS_ABS)
        if (NOT _IS_ABS)
            message(FATAL_ERROR "mkdir: 절대경로만 가능: ${_DIR}")
        endif ()

        cmake_path(NORMAL_PATH _DIR)                        # a/../b → b
        string(REGEX REPLACE "(.)/+$" "\\1" _DIR "${_DIR}") # 끝의 / 제거 (루트 "/" 는 유지)

        if (EXISTS "${_DIR}" AND NOT IS_DIRECTORY "${_DIR}")
            message(FATAL_ERROR "mkdir: 같은 이름의 파일이 있음: ${_DIR}")
        endif ()

        if (NOT IS_DIRECTORY "${_DIR}")
            file(MAKE_DIRECTORY "${_DIR}")
            message(STATUS "mkdir: 생성 ${_DIR}")
        endif ()
    endforeach ()
endfunction()
