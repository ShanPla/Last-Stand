# Runs after the browser build links: copies the page shell components next to
# the output and writes the real .wasm size into the page for the loading bar.
file(COPY "${WEB_DIR}/components" DESTINATION "${OUT_DIR}")
file(SIZE "${OUT_DIR}/index.wasm" LS_WASM_BYTES)
file(READ "${OUT_DIR}/index.html" LS_PAGE)
string(REPLACE "__LS_WASM_BYTES__" "${LS_WASM_BYTES}" LS_PAGE "${LS_PAGE}")
file(WRITE "${OUT_DIR}/index.html" "${LS_PAGE}")
