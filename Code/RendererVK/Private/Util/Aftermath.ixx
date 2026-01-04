export module RendererVK.Aftermath;
extern "C++" {

export import <Aftermath/GFSDK_Aftermath_Defines.h>;
export import <Aftermath/GFSDK_Aftermath.h>;
export import <Aftermath/GFSDK_Aftermath_GpuCrashDump.h>;
export import <Aftermath/GFSDK_Aftermath_GpuCrashDumpDecoding.h>;

#undef GFSDK_Aftermath_SUCCEED
export constexpr bool GFSDK_Aftermath_SUCCEED(int value)
{
    return (((value) & 0xFFF00000) != GFSDK_Aftermath_Result_Fail);
}

#undef MB_OK
export long MB_OK = 0x00000000L;
} // extern "C++"