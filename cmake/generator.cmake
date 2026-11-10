# 탬플릿을 이용하여 버전파일 만들기
# 프로젝트에 버전입력한뒤에만 사용가능 (project(... VERSION ...) 뒤에서 호출)
#
# generate_version_file(NAME <namespace> TARGETS <타깃>...)
#   NAME    : version.hpp 의 namespace 이름, 생성 폴더 이름 (generated/<NAME>/version.hpp)
#   TARGETS : include 경로를 추가할 타깃들
function(generate_version_file)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "TARGETS")

    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "generate_version_file: 모르는 인자: ${ARG_UNPARSED_ARGUMENTS}")
    endif ()
    if (NOT ARG_NAME)
        message(FATAL_ERROR "generate_version_file: NAME 없음")
    endif ()
    if (NOT ARG_TARGETS)
        message(FATAL_ERROR "generate_version_file: TARGETS 없음")
    endif ()
    if (NOT DEFINED PROJECT_VERSION OR PROJECT_VERSION STREQUAL "")
        message(FATAL_ERROR "generate_version_file: project() 에 VERSION 이 없음")
    endif ()

    # 파일은 한 번만 생성
    set(_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(PROGRAM_NAME "${ARG_NAME}")     # version.hpp.in 의 @PROGRAM_NAME@ 용 (함수 범위라 밖으로 안 샘)
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/version.hpp.in"
            "${_GEN_DIR}/${ARG_NAME}/version.hpp" @ONLY)

    # 타깃마다 include 경로 추가
    foreach (_TARGET IN LISTS ARG_TARGETS)
        if (NOT TARGET ${_TARGET})
            message(FATAL_ERROR "generate_version_file: 없는 타깃: ${_TARGET}")
        endif ()

        get_target_property(_TYPE ${_TARGET} TYPE)
        if (_TYPE STREQUAL "INTERFACE_LIBRARY")
            set(_SCOPE INTERFACE)       # 헤더 전용 라이브러리
        elseif (_TYPE STREQUAL "EXECUTABLE")
            set(_SCOPE PRIVATE)         # 실행 파일은 남한테 전달할 일이 없음
        else ()
            set(_SCOPE PUBLIC)          # STATIC / SHARED / OBJECT 라이브러리
        endif ()

        target_include_directories(${_TARGET} ${_SCOPE}
                "$<BUILD_INTERFACE:${_GEN_DIR}>")
    endforeach ()
endfunction()
