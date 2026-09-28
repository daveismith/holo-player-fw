idf_component_register(SRCS "webui.c" "webui_release.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES web_server web_api board_api)

# The web app (web/ at the top of the repository), plus two files it shares with the
# documentation site: the image inspector the installer uses, and the API's OpenAPI description
# (served at /api/v1/openapi.json). All gzipped into a C table at build time.
set(root "${CMAKE_CURRENT_LIST_DIR}/../..")
file(GLOB_RECURSE web_files CONFIGURE_DEPENDS "${root}/web/*")
set(shared "${root}/manual/javascripts/installer/inspect.js" "${root}/manual/reference/openapi.json")
set(specs)
foreach(f ${web_files})
    file(RELATIVE_PATH rel "${root}/web" "${f}")
    list(APPEND specs "/${rel}=${f}")
endforeach()
list(APPEND specs "/lib/inspect.js=${root}/manual/javascripts/installer/inspect.js")
list(APPEND specs "/openapi.json=${root}/manual/reference/openapi.json")

idf_build_get_property(python PYTHON)
set(out "${CMAKE_CURRENT_BINARY_DIR}/webui_assets.c")
add_custom_command(OUTPUT "${out}"
    COMMAND ${python} "${CMAKE_CURRENT_LIST_DIR}/embed_assets.py" "${out}" ${specs}
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/embed_assets.py" ${web_files} ${shared}
    VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE "${out}")
