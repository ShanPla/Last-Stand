cmake_minimum_required(VERSION 3.24)
project(LastStand LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LS_BUILD_TESTS "Build the native unit tests and bot run" ON)

# ---------------------------------------------------------------------------
# raylib: pinned version and hash. Stored in build/downloads so that deleting a
# single build folder does not download it again.
# ---------------------------------------------------------------------------
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
  set(PLATFORM "Web" CACHE STRING "" FORCE)
  set(GRAPHICS "GRAPHICS_API_OPENGL_ES3" CACHE STRING "" FORCE)
endif()
FetchContent_Declare(raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/6.2.tar.gz
  URL_HASH SHA256=df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee
  DOWNLOAD_NAME raylib-6.2.tar.gz
  DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/build/downloads"
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(raylib)

# ---------------------------------------------------------------------------
# The simulation is plain C++ with no raylib dependency, so that unit tests and
# the bot run can execute it without a window.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE LS_SIM_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp
  src/components/Player/*.cpp
  src/components/Enemies/*.cpp
  src/components/Weapons/*.cpp
  src/components/Pickups/*.cpp
  src/components/Upgrades/*.cpp
)
list(FILTER LS_SIM_SOURCES EXCLUDE REGEX "/core/(App|Platform)\\.cpp$")

add_library(ls_sim STATIC ${LS_SIM_SOURCES})
target_include_directories(ls_sim PUBLIC src)

file(GLOB_RECURSE LS_GAME_SOURCES CONFIGURE_DEPENDS
  src/components/Effects/*.cpp
  src/components/Audio/*.cpp
  src/components/Input/*.cpp
  src/components/Ui/*.cpp
  src/components/WorldView/*.cpp
)
file(GLOB LS_APP_SOURCE CONFIGURE_DEPENDS src/core/App.cpp)

add_executable(laststand src/main.cpp src/core/Platform.cpp ${LS_APP_SOURCE} ${LS_GAME_SOURCES})
target_link_libraries(laststand PRIVATE ls_sim raylib)

if(MSVC)
  target_compile_options(ls_sim PRIVATE /W4 /utf-8)
  target_compile_options(laststand PRIVATE /W4 /utf-8)
  target_link_libraries(laststand PRIVATE winmm)
endif()

# ---------------------------------------------------------------------------
# Browser build
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".html")
  set(LS_WEB_DIR "${CMAKE_SOURCE_DIR}/web")
  foreach(tgt ls_sim laststand)
    target_compile_options(${tgt} PRIVATE -O3 -flto -fno-exceptions -fno-rtti)
  endforeach()
  target_compile_options(raylib PRIVATE -O3 -flto)
  target_link_options(laststand PRIVATE
    -O3 -flto
    "--shell-file=${LS_WEB_DIR}/index.html"
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sENVIRONMENT=web
    -sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1MB
    -sEXPORTED_FUNCTIONS=_main,_ls_query_state,_ls_debug_command
    -fno-exceptions -fno-rtti
  )
  set_target_properties(laststand PROPERTIES OUTPUT_NAME "index"
    LINK_DEPENDS "${LS_WEB_DIR}/index.html")
  # Copies the page shell next to the build output and writes the real
  # .wasm size into the page, so the loading bar can show true progress.
  add_custom_command(TARGET laststand POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DOUT_DIR=$<TARGET_FILE_DIR:laststand>
      -DWEB_DIR=${LS_WEB_DIR}
      -P "${CMAKE_SOURCE_DIR}/web/FinishWebBuild.cmake"
  )
endif()

# ---------------------------------------------------------------------------
# Native tests
# ---------------------------------------------------------------------------
if(LS_BUILD_TESTS AND NOT EMSCRIPTEN)
  enable_testing()
  file(GLOB_RECURSE LS_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
  add_executable(ls_tests ${LS_TEST_SOURCES})
  target_link_libraries(ls_tests PRIVATE ls_sim)
  target_include_directories(ls_tests PRIVATE third_party/doctest)
  if(MSVC)
    target_compile_options(ls_tests PRIVATE /W4 /utf-8)
  endif()
  add_test(NAME unit COMMAND ls_tests)
endif()
