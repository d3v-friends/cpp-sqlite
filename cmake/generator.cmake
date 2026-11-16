include_guard(GLOBAL)

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

# 폴더 안의 헤더를 전부 include 하는 헤더(umbrella header) 생성
#
# generate_header(
#         OUTPUT    <생성할 헤더 절대경로>
#         DIRECTORY <헤더를 모을 폴더 절대경로>
#         BASE_DIR  <include 기준 폴더 절대경로>      # 예) .../include → "starter/a.hpp"
#         [EXCLUDE  <정규식>...]                       # 예) "/detail/" "_test\\.hpp$"
# )
#   - 하위 폴더까지 재귀로 찾음 (.h .hpp .hxx .hh)
#   - 다른 플랫폼 접미사(_apple/_win32/_android/_linux) 헤더는 자동 제외
#   - 내용이 같으면 파일을 다시 쓰지 않음 (불필요한 재빌드 방지)
function(generate_header)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "OUTPUT;DIRECTORY;BASE_DIR" "EXCLUDE")

    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "generate_header: 모르는 인자: ${ARG_UNPARSED_ARGUMENTS}")
    endif ()
    foreach (_KEY OUTPUT DIRECTORY BASE_DIR)
        if (NOT ARG_${_KEY})
            message(FATAL_ERROR "generate_header: ${_KEY} 없음")
        endif ()
        cmake_path(IS_ABSOLUTE ARG_${_KEY} _IS_ABS)
        if (NOT _IS_ABS)
            message(FATAL_ERROR "generate_header: ${_KEY} 는 절대경로만 가능: ${ARG_${_KEY}}")
        endif ()
        cmake_path(NORMAL_PATH ARG_${_KEY})
    endforeach ()
    if (NOT IS_DIRECTORY "${ARG_DIRECTORY}")
        message(FATAL_ERROR "generate_header: 폴더 아님: ${ARG_DIRECTORY}")
    endif ()

    # DIRECTORY 가 BASE_DIR 안에 있어야 상대경로 include 가 성립
    cmake_path(IS_PREFIX ARG_BASE_DIR "${ARG_DIRECTORY}" NORMALIZE _IN_BASE)
    if (NOT _IN_BASE)
        message(FATAL_ERROR "generate_header: DIRECTORY 가 BASE_DIR 밖에 있음\n"
                "  DIRECTORY: ${ARG_DIRECTORY}\n  BASE_DIR : ${ARG_BASE_DIR}")
    endif ()

    # 헤더 수집
    # CONFIGURE_DEPENDS 는 configure 모드에서만 허용 (cmake -P 스크립트 모드면 빼기)
    set(_GLOB_FLAGS "")
    if (NOT CMAKE_SCRIPT_MODE_FILE)
        set(_GLOB_FLAGS CONFIGURE_DEPENDS)
    endif ()
    file(GLOB_RECURSE _HEADERS ${_GLOB_FLAGS}
            "${ARG_DIRECTORY}/*.h" "${ARG_DIRECTORY}/*.hpp"
            "${ARG_DIRECTORY}/*.hxx" "${ARG_DIRECTORY}/*.hh")

    # 자기 자신 제외: 실제 경로로 비교 (심볼릭 링크, 대소문자 차이 대응)
    # + 예전에 generate_header 가 만든 파일도 제외 (OUTPUT 이름을 바꿨을 때 남은 것)
    file(REAL_PATH "${ARG_OUTPUT}" _OUT_REAL)
    string(TOLOWER "${_OUT_REAL}" _OUT_REAL)

    set(_FILTERED "")
    foreach (_HEADER IN LISTS _HEADERS)
        file(REAL_PATH "${_HEADER}" _REAL)
        string(TOLOWER "${_REAL}" _REAL)
        if (_REAL STREQUAL _OUT_REAL)
            continue()
        endif ()

        file(STRINGS "${_HEADER}" _FIRST LIMIT_COUNT 1)
        if (_FIRST MATCHES "^// 자동 생성 파일 \\(generate_header\\)")
            continue()
        endif ()

        list(APPEND _FILTERED "${_HEADER}")
    endforeach ()
    set(_HEADERS ${_FILTERED})

    # 다른 플랫폼 헤더 제외
    if (APPLE)
        set(_SUFFIX apple)
    elseif (WIN32)
        set(_SUFFIX win32)
    elseif (ANDROID)
        set(_SUFFIX android)
    else ()
        set(_SUFFIX linux)
    endif ()
    set(_OTHERS apple win32 android linux)
    list(REMOVE_ITEM _OTHERS ${_SUFFIX})
    list(JOIN _OTHERS "|" _OTHERS_REGEX)
    list(FILTER _HEADERS EXCLUDE REGEX "_(${_OTHERS_REGEX})\\.(h|hpp|hxx|hh)$")

    # 사용자 제외 규칙
    foreach (_REGEX IN LISTS ARG_EXCLUDE)
        list(FILTER _HEADERS EXCLUDE REGEX "${_REGEX}")
    endforeach ()

    if (NOT _HEADERS)
        message(WARNING "generate_header: 헤더가 없음: ${ARG_DIRECTORY}")
    endif ()

    # 순서를 고정해야 내용이 매번 같음 → 재빌드 안 일어남
    list(SORT _HEADERS)

    set(_CONTENT "// 자동 생성 파일 (generate_header)\n#pragma once\n\n")
    foreach (_HEADER IN LISTS _HEADERS)
        cmake_path(RELATIVE_PATH _HEADER BASE_DIRECTORY "${ARG_BASE_DIR}" OUTPUT_VARIABLE _REL)
        string(APPEND _CONTENT "#include \"${_REL}\"\n")
    endforeach ()

    # file(CONFIGURE) 는 내용이 바뀌었을 때만 파일을 씀
    file(CONFIGURE OUTPUT "${ARG_OUTPUT}" CONTENT "${_CONTENT}" @ONLY)

    list(LENGTH _HEADERS _COUNT)
    message(STATUS "generate_header: ${ARG_OUTPUT} (${_COUNT}개)")
endfunction()

