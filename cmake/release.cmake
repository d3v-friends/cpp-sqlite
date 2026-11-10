# Windows exe 에 리소스(아이콘/버전정보) + 매니페스트 추가
# Release 에서만 콘솔창 없이 실행 (main() 그대로 사용)
#
# release_win32(<타깃> RC_IN <app.rc.in> MANIFEST <app.manifest>)
function(release_win32 _TARGET)
    if (NOT WIN32)
        message(TRACE "release_win32: 윈도우 환경이 아닙니다.")
        return()
    endif ()

    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "RC_IN;MANIFEST" "")   # ⑥칸도 "" 로 채움

    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "release_win32: 모르는 인자: ${ARG_UNPARSED_ARGUMENTS}")
    endif ()
    if (NOT TARGET ${_TARGET})
        message(FATAL_ERROR "release_win32: 없는 타깃: ${_TARGET}")
    endif ()
    foreach (_KEY RC_IN MANIFEST)
        if (NOT ARG_${_KEY} OR NOT EXISTS "${ARG_${_KEY}}")
            message(FATAL_ERROR "release_win32: ${_KEY} 파일 없음: '${ARG_${_KEY}}'")
        endif ()
    endforeach ()

    # rc 는 빌드 폴더에 생성 (소스 트리 오염 방지)
    set(_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/win32/${_TARGET}.rc")
    configure_file("${ARG_RC_IN}" "${_RC}" @ONLY)

    target_sources(${_TARGET} PRIVATE
            "${_RC}"              # ← .in 이 아니라 생성된 rc
            "${ARG_MANIFEST}"
    )

    # Release 만 GUI 서브시스템 + main() 진입점 유지
    if (MSVC)
        target_link_options(${_TARGET} PRIVATE
                "$<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>"
                "$<$<CONFIG:Release>:/ENTRY:mainCRTStartup>")
    else ()   # MinGW
        target_link_options(${_TARGET} PRIVATE "$<$<CONFIG:Release>:-mwindows>")
    endif ()
endfunction()
