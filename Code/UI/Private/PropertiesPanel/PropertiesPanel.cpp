module UI.PropertiesPanel;

import Core.imgui;

void PropertiesPanel::render(SceneNode* selected)
{
	if (!selected)
	{
		ImGui::TextDisabled("No entity selected");
		return;
	}

	// ---- Name ---------------------------------------------------------------
	if (ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen))
	{
		char nameBuf[256];
		strncpy_s(nameBuf, sizeof(nameBuf), selected->name.c_str(), sizeof(nameBuf) - 1);
		nameBuf[sizeof(nameBuf) - 1] = '\0';

		ImGui::AlignTextToFramePadding();
		ImGui::Text("Name");
		ImGui::SameLine(80.0f);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##pp_name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			if (nameBuf[0] != '\0')
				selected->name = nameBuf;
		}

		ImGui::AlignTextToFramePadding();
		ImGui::Text("Enabled");
		ImGui::SameLine(80.0f);
		ImGui::Checkbox("##pp_enabled", &selected->enabled);

		if (selected->entityId != 0)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Entity ID");
			ImGui::SameLine(80.0f);
			ImGui::TextDisabled("0x%llX", static_cast<unsigned long long>(selected->entityId));
		}
	}

	// ---- Transform placeholder ----------------------------------------------
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::TextDisabled("Not yet implemented");
	}
}
