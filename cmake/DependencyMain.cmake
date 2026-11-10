# add the parent dir as an include path
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH PARENT_DIR)
message(STATUS "AsyncLoad: adding ${PARENT_DIR} as an include path")

target_include_directories(${proname} PUBLIC ${PARENT_DIR})

# hacky! check if dependency is a required one
file(READ "${dir}/geode-dep-options.json" DEP_JSON)
string(JSON required GET "${DEP_JSON}" "required")
if (${required})
    target_compile_definitions(${proname} PUBLIC ASYNCLOAD_LINKING)
endif()