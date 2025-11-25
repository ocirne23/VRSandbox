module Script;

import Core.imgui;

Script::Script() {}
Script::~Script() {}

bool Script::initialize()
{
    return true;
}

void Script::update(double deltaSec)
{
}

void Script::updateUI(double deltaSec)
{
    {
        ImGui::Begin("Script");
       // const ImGuiContext* ctx = ImGui::GetCurrentContext();
        //ctx->CurrentWindow->DrawList->AddRectFilled(ImVec2(50, 50), ImVec2(500, 500), ImColor(255, 0, 0));

        ImGui::Button("Button 1");
        ImGui::Button("Button 2");
        ImGui::End();
    }
}