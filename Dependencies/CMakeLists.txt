# box3d is vendored source built as part of the solution (everything else here is prebuilt Include/Lib/Dll).
# Added from the root before add_definitions so its C sources don't get the engine's forceinclude/warning flags.
add_subdirectory(box3d)

# box3d defaults to the static CRT; the engine and its prebuilt dependencies use the DLL runtime.
set_property(TARGET box3d PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(TARGET box3d PROPERTY FOLDER Dependencies)
