cmake_minimum_required (VERSION 3.14)

project(VRSandbox CXX)

set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")

include(Utils.cmake)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_Build")

list(APPEND COMPILE_OPTIONS "/std:c++latest" "/Zc:__cplusplus" "/fp:fast" "/arch:AVX2" "$<$<NOT:$<CONFIG:DEBUG>>:/GL>" "/Oi" "/Ot" "/W4")
list(APPEND LINK_OPTIONS "$<$<NOT:$<CONFIG:DEBUG>>:/LTCG>" "$<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO>")

add_definitions(/FI"${ROOT_DIR}/Code/Core/Private/forceinclude.h"  
	/wd4100 # unreferenced formal parameter
	/wd5260 # the constant variable 'ai_epsilon' has internal linkage in an included header file context, but external linkage in imported header unit context
	/wd5105 # macro expansion producing 'defined' has undefined behavior
)

add_subdirectory(Code)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT "App")