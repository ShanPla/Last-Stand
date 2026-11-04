# Runs after the browser build links. It completes the site in the output folder:
# the page shell components, the manifest, generated icons, the offline worker,
# and a list of every file that belongs on the web host.
#
# Inputs: OUT_DIR (build output), WEB_DIR (the web folder), TOOLS_DIR (the tools folder).
file(COPY "${WEB_DIR}/components" DESTINATION "${OUT_DIR}")
file(COPY "${WEB_DIR}/manifest.webmanifest" DESTINATION "${OUT_DIR}")

# Icons are drawn by a script at build time, so no image is ever committed.
find_program(LS_NODE node)
if(NOT LS_NODE)
  message(FATAL_ERROR "node was not found on PATH. It is needed to generate the app icons.")
endif()
execute_process(
  COMMAND "${LS_NODE}" "${TOOLS_DIR}/generate-icons.mjs" "${OUT_DIR}/icons"
  RESULT_VARIABLE LS_ICONS_RESULT)
if(NOT LS_ICONS_RESULT EQUAL 0)
  message(FATAL_ERROR "Generating the app icons failed.")
endif()

# The loading bar needs the real .wasm size.
file(SIZE "${OUT_DIR}/index.wasm" LS_WASM_BYTES)
file(READ "${OUT_DIR}/index.html" LS_PAGE)
string(REPLACE "__LS_WASM_BYTES__" "${LS_WASM_BYTES}" LS_PAGE "${LS_PAGE}")
file(WRITE "${OUT_DIR}/index.html" "${LS_PAGE}")

# Every file the game needs, relative to the output folder. The offline worker
# caches exactly this list and the deploy step copies exactly this list, so
# build system files (CMakeFiles, build.ninja, ...) never reach the host.
file(GLOB_RECURSE LS_NESTED RELATIVE "${OUT_DIR}" "${OUT_DIR}/components/*" "${OUT_DIR}/icons/*")
list(SORT LS_NESTED)
set(LS_SITE_FILES index.html index.js index.wasm manifest.webmanifest ${LS_NESTED})

# The build id is a hash of all those files plus the worker itself: the same
# content always gives the same id, and any change gives a new offline cache.
file(READ "${WEB_DIR}/sw.js" LS_WORKER)
set(LS_HASHES "${LS_WORKER}")
set(LS_PRECACHE "")
foreach(item IN LISTS LS_SITE_FILES)
  file(SHA256 "${OUT_DIR}/${item}" LS_FILE_HASH)
  string(APPEND LS_HASHES "${item}=${LS_FILE_HASH};")
  string(APPEND LS_PRECACHE "\"${item}\",")
endforeach()
string(SHA256 LS_BUILD_ID "${LS_HASHES}")
string(SUBSTRING "${LS_BUILD_ID}" 0 12 LS_BUILD_ID)
string(REGEX REPLACE ",$" "" LS_PRECACHE "${LS_PRECACHE}")
string(REPLACE "__LS_BUILD_ID__" "${LS_BUILD_ID}" LS_WORKER "${LS_WORKER}")
string(REPLACE "__LS_PRECACHE__" "[${LS_PRECACHE}]" LS_WORKER "${LS_WORKER}")
file(WRITE "${OUT_DIR}/sw.js" "${LS_WORKER}")

# One file per line, for the deploy step. The worker is listed too.
string(REPLACE ";" "\n" LS_SITE_LIST "${LS_SITE_FILES};sw.js")
file(WRITE "${OUT_DIR}/site-files.txt" "${LS_SITE_LIST}\n")
