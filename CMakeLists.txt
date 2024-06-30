cmake_minimum_required (VERSION 3.14)

project(VRSandbox CXX)

set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")

include(Utils.cmake)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_Build")

list(APPEND COMPILE_OPTIONS "/std:c++latest" "/experimental:module" "/Zc:__cplusplus" "/fp:fast" "/arch:AVX2" "$<$<NOT:$<CONFIG:DEBUG>>:/GL>" "/Oi" "/Ot")
list(APPEND LINK_OPTIONS "$<$<NOT:$<CONFIG:DEBUG>>:/LTCG>" "$<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO>")

add_definitions(/FI"cassert" /wd5260 /wd5105)

add_subdirectory(Code)