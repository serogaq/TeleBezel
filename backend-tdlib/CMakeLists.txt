cmake_minimum_required(VERSION 3.25)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TELEBEZEL_SOURCE_VERSION LIMIT_COUNT 1)
project(telebezel_backend_tdlib VERSION "${TELEBEZEL_SOURCE_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(TELEBEZEL_BUILD_TESTS "Build TeleBezel unit tests" ON)
option(TELEBEZEL_SANITIZERS "Instrument adapter tests with ASan and UBSan" OFF)

include(cmake/Dependencies.cmake)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(telebezel-tdlib
  src/config.cpp
  src/crypto.cpp
  src/http_server.cpp
  src/main.cpp
  src/registry.cpp
  src/status.cpp
  src/td_runtime.cpp
  src/transport.cpp)
target_include_directories(telebezel-tdlib PRIVATE include)
target_include_directories(telebezel-tdlib SYSTEM BEFORE PRIVATE
  "${tdlib_SOURCE_DIR}"
  "${tdlib_SOURCE_DIR}/td/generate/auto"
  "${json_SOURCE_DIR}/include")
target_link_libraries(telebezel-tdlib PRIVATE Td::TdStatic httplib::httplib nlohmann_json::nlohmann_json OpenSSL::Crypto Threads::Threads)
target_compile_definitions(telebezel-tdlib PRIVATE TELEBEZEL_SERVICE_VERSION="${PROJECT_VERSION}")
target_compile_options(telebezel-tdlib PRIVATE $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall;-Wextra;-Wpedantic;-Werror>)

if(TELEBEZEL_BUILD_TESTS)
  enable_testing()
  add_executable(telebezel-tdlib-tests tests/status_test.cpp src/config.cpp src/crypto.cpp src/registry.cpp src/status.cpp)
  target_include_directories(telebezel-tdlib-tests PRIVATE include)
  target_link_libraries(telebezel-tdlib-tests PRIVATE nlohmann_json::nlohmann_json OpenSSL::Crypto)
  target_compile_definitions(telebezel-tdlib-tests PRIVATE TELEBEZEL_SERVICE_VERSION="${PROJECT_VERSION}")
  target_compile_definitions(telebezel-tdlib-tests PRIVATE TELEBEZEL_CONTRACT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/contracts")
  target_compile_options(telebezel-tdlib-tests PRIVATE $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall;-Wextra;-Wpedantic;-Werror>)
  if(TELEBEZEL_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(telebezel-tdlib-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(telebezel-tdlib-tests PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME telebezel-tdlib-tests COMMAND telebezel-tdlib-tests)
  add_executable(telebezel-tdlib-runtime-tests tests/runtime_test.cpp src/config.cpp src/crypto.cpp src/registry.cpp
    src/status.cpp src/td_runtime.cpp src/transport.cpp)
  target_include_directories(telebezel-tdlib-runtime-tests PRIVATE include)
  target_include_directories(telebezel-tdlib-runtime-tests SYSTEM BEFORE PRIVATE
    "${tdlib_SOURCE_DIR}" "${tdlib_SOURCE_DIR}/td/generate/auto" "${json_SOURCE_DIR}/include")
  target_link_libraries(telebezel-tdlib-runtime-tests PRIVATE Td::TdStatic nlohmann_json::nlohmann_json OpenSSL::Crypto Threads::Threads)
  target_compile_definitions(telebezel-tdlib-runtime-tests PRIVATE TELEBEZEL_SERVICE_VERSION="${PROJECT_VERSION}")
  target_compile_options(telebezel-tdlib-runtime-tests PRIVATE $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall;-Wextra;-Wpedantic;-Werror>)
  if(TELEBEZEL_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(telebezel-tdlib-runtime-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(telebezel-tdlib-runtime-tests PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME telebezel-tdlib-runtime-tests COMMAND telebezel-tdlib-runtime-tests)
endif()
