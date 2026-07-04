# Unused: all dependencies are prebuilt into Include/Lib/Dll. box3d source is kept in box3d/ for
# reference and for rebuilding the prebuilt libs (box3d.lib / box3dd.lib in Lib, headers in
# Include/box3d) — build it standalone with the DLL CRT ("MultiThreaded$<$<CONFIG:Debug>:Debug>DLL",
# its own CMakeLists defaults to the static CRT) and copy the outputs over.
