# JSON 파일에서 키에 연결된 문자열 값을 가져온다
#
# get_json(<OUT_VAR> <KEY>... [FILE <json경로>] [DEFAULT <기본값>])
#   KEY     : 키 경로. 중첩 키는 순서대로 나열 (예: features tests description)
#   FILE    : json 위치
#   DEFAULT : 키가 없을 때 쓸 값. 지정 안 하면 FATAL_ERROR
function(get_json _OUT_VAR)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "FILE;DEFAULT" "")
    set(_KEYS ${ARG_UNPARSED_ARGUMENTS})       # 키워드가 아닌 나머지 = 키 경로

    if (NOT _KEYS)
        message(FATAL_ERROR "get_json: KEY 없음")
    endif ()

    if (NOT ARG_FILE)
        message(FATAL_ERROR "get_json: FILE 없음 ${ARG_FILE}")
    endif ()

    if (NOT EXISTS "${ARG_FILE}")
        message(FATAL_ERROR "get_json: 파일 없음: ${ARG_FILE}")
    endif ()

    # json 파일이 바뀌면 다시 configure
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ARG_FILE}")

    file(READ "${ARG_FILE}" _JSON)
    list(JOIN _KEYS "." _KEY_PATH)              # 에러 메시지용

    # 값 종류 확인 (없는 키면 _ERR 에 메시지가 들어감)
    string(JSON _TYPE ERROR_VARIABLE _ERR TYPE "${_JSON}" ${_KEYS})
    if (_ERR)
        if (DEFINED ARG_DEFAULT)
            set(${_OUT_VAR} "${ARG_DEFAULT}" PARENT_SCOPE)
            return()
        endif ()
        message(FATAL_ERROR "get_json: '${_KEY_PATH}' 키 없음 (${ARG_FILE})")
    endif ()

    # 문자열, 숫자만 허용 (숫자는 "20" 같은 텍스트로 받아짐)
    if (NOT _TYPE MATCHES "^(STRING|NUMBER)$")
        message(FATAL_ERROR "get_json: '${_KEY_PATH}' 은 문자열/숫자가 아님 (${_TYPE})")
    endif ()

    string(JSON _VALUE GET "${_JSON}" ${_KEYS})
    set(${_OUT_VAR} "${_VALUE}" PARENT_SCOPE)
endfunction()
