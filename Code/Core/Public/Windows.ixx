export module Core.Windows;

export import <Windows.h>;

// Win32 #defines don't cross the named-module boundary, so re-expose the ones we use as
// typed constants. (#undef first in case the header unit makes them active macros here.)
#undef FALSE
#undef TRUE
#undef INFINITE
#undef CREATE_NO_WINDOW

export inline constexpr BOOL  FALSE = 0;
export inline constexpr BOOL  TRUE = 1;
export inline constexpr DWORD INFINITE = 0xFFFFFFFF;
export inline constexpr DWORD CREATE_NO_WINDOW = 0x08000000;