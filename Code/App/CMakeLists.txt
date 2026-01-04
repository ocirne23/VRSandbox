project(App)

file(GLOB_RECURSE PROJECT_PRIVATE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${PROJECT_PRIVATE_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER Apps)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ignore:4099")

target_link_libraries(${PROJECT_NAME} PRIVATE Core RendererVK UI Entity File Input Scene)

apply_compile_settings(${PROJECT_NAME})