project(RendererVK)

file(GLOB_RECURSE PROJECT_PRIVATE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.ixx" "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp")
file(GLOB_RECURSE PROJECT_PUBLIC_FILES  "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.ixx")

add_library(${PROJECT_NAME} STATIC ${PROJECT_PRIVATE_FILES} ${PROJECT_PUBLIC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_include_directories(${PROJECT_NAME} PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/Public")

target_link_libraries(${PROJECT_NAME} PUBLIC Core File vulkan-1
	glslang$<$<CONFIG:Debug>:d> 
	MachineIndependent$<$<CONFIG:Debug>:d>
	OSDependent$<$<CONFIG:Debug>:d>
	GenericCodeGen$<$<CONFIG:Debug>:d>
	glslang-default-resource-limits$<$<CONFIG:Debug>:d>
	shaderc$<$<CONFIG:Debug>:d>
	SPIRV$<$<CONFIG:Debug>:d>
	SPIRV-Tools$<$<CONFIG:Debug>:d>
	SPIRV-Tools-opt$<$<CONFIG:Debug>:d>
	GFSDK_Aftermath_Lib.x64
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER Engine)

apply_compile_settings(${PROJECT_NAME})