# 타겟 머신별 특수 설정 추가하기
function(target_settings _TARGET)
    # 컴파일러 버전 지정한 버전으로 강제하기
    target_compile_features(${_TARGET} PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

    # windows 에서 실행시 필요한 라이브러리 추가
    if (WIN32)
        target_link_libraries(${_TARGET} PRIVATE bcrypt)
        target_compile_definitions(${_TARGET} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif ()

    # utf-8 기본으로 사용하기
    if (MSVC)
        target_compile_options(${_TARGET} PRIVATE /W4 /utf-8 /permissive-)
    else ()
        target_compile_options(${_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()

endfunction()

function(is_available_cpp_version _VERSION)
    set(_KNOWN_STD 17 20 23 26)
    if (NOT _VERSION IN_LIST _KNOWN_STD)
        message(FATAL_ERROR "is_available_cpp_version: '${_VERSION}' (${_KNOWN_STD} 지원하지 않는 CXX 버전")
    endif ()
endfunction()