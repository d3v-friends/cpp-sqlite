# vcpkg.json 에서 버전명을 가져온다. 변수명은 APP_VERSION
set(VCPKG_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")
file(READ "${VCPKG_MANIFEST}" VCPKG_JSON)
foreach (KEY version version-semver version-date version-string)
    string(JSON APP_VERSION ERROR_VARIABLE JSON_ERR GET "${VCPKG_JSON}" ${KEY})  # APP_VERSION 생성
    if (NOT JSON_ERR)
        break()
    endif ()
endforeach ()

if (JSON_ERR)
    message(FATAL_ERROR "vcpkg.json 에 버전 정보가 없음")
endif ()

string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" APP_VERSION "${APP_VERSION}")   # 숫자 부분만 남김
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VCPKG_MANIFEST}")
message(STATUS "APP_VERSION = ${APP_VERSION}")