export module Core.imgui;
extern "C++" {

import Core;

export import <imgui/imgui.h>;
export import <imgui/imgui_internal.h>;
export import <imgui/imgui_impl_sdl3.h>;
export import <imgui/imgui_impl_vulkan.h>;

export void imgui_check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    printf("[vulkan] Error: imgui VkResult = %d\n", err);
    if (err < 0)
        __debugbreak();
}
} // extern "C++"