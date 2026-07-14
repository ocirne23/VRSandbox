project(DiffusionTest)

# TEMPORARY bring-up harness for the V3 / diffusion terrain stack (M1). Delete once TerrainGenV3 is wired
# into the App and can be exercised there.
file(GLOB_RECURSE PROJECT_PRIVATE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${PROJECT_PRIVATE_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER Apps)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ignore:4099")

target_link_libraries(${PROJECT_NAME} PRIVATE Core File Procedural)

apply_compile_settings(${PROJECT_NAME})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Dependencies/Dll/onnxruntime.dll"
        "${CMAKE_SOURCE_DIR}/Dependencies/Dll/DirectML.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
