project(DslCompiler)

file(GLOB_RECURSE PROJECT_PRIVATE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${PROJECT_PRIVATE_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER _Build)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ignore:4099")

# Entity is what carries registerScriptDslBindings (the whole DSL vocabulary), and its public link surface
# pulls in everything it depends on (RendererVK, File, Script, Physics, ...). Nothing gets initialized at
# runtime -- the tool only needs the registration call and Script's loader/transpiler. See CONTEXT.md.
target_link_libraries(${PROJECT_NAME} PRIVATE Core Entity Script File)

apply_compile_settings(${PROJECT_NAME})

# RendererVK links the Aftermath import lib (load-time DLL dependency), so the DLL must sit next to the exe --
# same reason App copies it.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/Dependencies/Dll/GFSDK_Aftermath_Lib.x64.dll"
		"$<TARGET_FILE_DIR:${PROJECT_NAME}>")
