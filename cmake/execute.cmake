# 프로젝트를 라이브러리로 활성화 하기
# _OUT_VAR 출력할 변수
# _LIB_NAME 라이브러리 이름
# _PACKAGES 링크할 패키지들
# _SOURCES 추가 파일들
function(make_library _OUT_VAR)
    # 파라미터 이름 방식으로 변수 받아오기
    # PARSE_ARGV 함수 인자(ARGV0, ARGV1…)를 직접 읽는 방식 (function 안에서만 쓸 수 있음)
    # 1 0번 인자(OUT_VAR)는 건너뛰고 1번부터 파싱
    # ARG 결과 변수 앞에 붙는 prefix → ARG_NAME, ARG_SOURCES
    # 옵션 키워드 (값 없이 켜고 끄는 것). 지금은 없어서 빈 문자열
    # "NAME" 키워드 여기서는 1개값만 정확히 뽑아오기 위해서 따로 지정함
    # "PACKAGES;SOURCES" 여러개 설정가능
    cmake_parse_arguments(
            PARSE_ARGV 1 ARG "" "LIB_NAME" "PACKAGES;SOURCES"
    )

    if (NOT ARG_LIB_NAME)
        message(FATAL_ERROR "make_library: LIB_NAME 없음")
    endif ()

    if (NOT ARG_SOURCES)
        message(FATAL_ERROR "make_library: SOURCES 없음")
    endif ()

    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "make_library: 모르는 인자: ${ARG_UNPARSED_ARGUMENTS}")
    endif ()


    add_library(${ARG_LIB_NAME} ${ARG_SOURCES})
    add_library(${ARG_LIB_NAME}::${ARG_LIB_NAME} ALIAS ${ARG_LIB_NAME})
    target_link_libraries(${ARG_LIB_NAME} PUBLIC ${ARG_PACKAGES})
    target_include_directories(${ARG_LIB_NAME} PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
    set(${_OUT_VAR} ${ARG_LIB_NAME} PARENT_SCOPE)
endfunction()

# 프로젝트를 실행파일로 활성화 하기
# PROGRAM 변수명
# SOURCES 파일들
function(make_executable _OUT_VAR)
    cmake_parse_arguments(
            PARSE_ARGV 1 ARG "" "EXE_NAME" "PACKAGES;SOURCES"
    )

    if (NOT ARG_EXE_NAME)
        message(FATAL_ERROR "make_executable: NAME 없음")
    endif ()

    if (NOT ARG_SOURCES)
        message(FATAL_ERROR "make_executable: SOURCES 없음")
    endif ()

    add_executable(${ARG_EXE_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_EXE_NAME} PRIVATE ${ARG_PACKAGES})
    target_include_directories(${ARG_EXE_NAME} PRIVATE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
    set(${_OUT_VAR} ${ARG_EXE_NAME} PARENT_SCOPE)
endfunction()