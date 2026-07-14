project(App)

file(GLOB_RECURSE PROJECT_PRIVATE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${PROJECT_PRIVATE_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER Apps)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ignore:4099")

target_link_libraries(${PROJECT_NAME} PRIVATE Core Animation RendererVK UI Entity File Input Script Physics Network Audio Spatial Procedural)

apply_compile_settings(${PROJECT_NAME})

# onnxruntime.dll imports DirectML.dll by name, so both must sit next to the exe: the AddDllDirectory
# in FileSystem::initialize only covers LoadLibraryEx(LOAD_LIBRARY_SEARCH_USER_DIRS), not load-time imports.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Dependencies/Dll/GFSDK_Aftermath_Lib.x64.dll"
        "${CMAKE_SOURCE_DIR}/Dependencies/Dll/onnxruntime.dll"
        "${CMAKE_SOURCE_DIR}/Dependencies/Dll/DirectML.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>")