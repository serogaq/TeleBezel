include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.cmake)

set(FETCHCONTENT_QUIET OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(TD_ENABLE_JNI OFF CACHE BOOL "" FORCE)

FetchContent_Declare(tdlib
  URL "https://github.com/tdlib/td/archive/${TELEBEZEL_TDLIB_COMMIT}.tar.gz"
  URL_HASH "SHA256=${TELEBEZEL_TDLIB_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(httplib
  URL "https://github.com/yhirose/cpp-httplib/archive/${TELEBEZEL_HTTPLIB_COMMIT}.tar.gz"
  URL_HASH "SHA256=${TELEBEZEL_HTTPLIB_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(json
  URL "https://github.com/nlohmann/json/archive/${TELEBEZEL_JSON_COMMIT}.tar.gz"
  URL_HASH "SHA256=${TELEBEZEL_JSON_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# TDLib currently declares C++17-compatible sources that are not char8_t-clean
# under Apple Clang. Keep the adapter on C++20 while isolating upstream at C++17.
set(_TELEBEZEL_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
set(CMAKE_CXX_STANDARD 17)
FetchContent_MakeAvailable(tdlib)
set(CMAKE_CXX_STANDARD "${_TELEBEZEL_CXX_STANDARD}")
FetchContent_MakeAvailable(httplib json)
