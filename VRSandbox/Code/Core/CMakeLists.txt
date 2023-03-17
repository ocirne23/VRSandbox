set(PROJECT_NAME Core)

project(${PROJECT_NAME})

file(GLOB_RECURSE PROJECT_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${PROJECT_FILES} )

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "1.Modules")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)

target_compile_options(${PROJECT_NAME} PRIVATE /experimental:module /Zc:__cplusplus)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/)

target_include_directories(${PROJECT_NAME} PRIVATE 
	${SDL2_INCLUDE_DIR}
	"${CMAKE_SOURCE_DIR}/Components/Hlms/Common/include"
	"${CMAKE_SOURCE_DIR}/Components/Hlms/Unlit/include" 
	"${CMAKE_SOURCE_DIR}/Components/Hlms/Pbs/include" 
	"${CMAKE_SOURCE_DIR}/Components/Overlay/include" )
target_link_libraries(${PROJECT_NAME} OgreMain RenderSystem_Direct3D11 OgreHlmsPbs OgreHlmsUnlit OgreOverlay ${SDL2_LIBRARY} ${OpenVR_LIBRARIES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Dependencies/include/Bullet3)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Dependencies/include/entt)

target_link_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Dependencies/lib/$<CONFIG>)
target_link_libraries(${PROJECT_NAME} BulletDynamics BulletCollision LinearMath)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PROJECT_FILES})
