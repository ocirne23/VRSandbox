module UI;

import Core;
import Core.imgui;
import Core.glm;
import Entity;
import RendererVK;
import :PropertiesPanel;

void PropertiesPanel::render(Entity* selected)
{
	if (!selected)
	{
		ImGui::TextDisabled("No entity selected");
		return;
	}

	SceneComponent* sc = getComponent<SceneComponent>(selected);

	if (ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Name");
		ImGui::SameLine(80.0f);
		ImGui::SetNextItemWidth(-1.0f);
		{
			char nameBuf[256];
			strncpy_s(nameBuf, sizeof(nameBuf), selected->displayName.c_str(), sizeof(nameBuf) - 1);
			nameBuf[sizeof(nameBuf) - 1] = '\0';
			if (ImGui::InputText("##pp_name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
				selected->displayName = nameBuf;
		}

		{
			const std::string& sourceFile = selected->getSourceFile();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Asset");
			ImGui::SameLine(80.0f);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::TextWrapped("%s", sourceFile.empty() ? "<inline>" : sourceFile.c_str());
		}

		if (sc)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Enabled");
			ImGui::SameLine(80.0f);
			ImGui::Checkbox("##pp_enabled", &sc->enabled);
		}
	}

	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Position");
		ImGui::SameLine(80.0f);
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::DragFloat3("##pp_pos", &selected->pos.x, 0.05f);

		ImGui::AlignTextToFramePadding();
		ImGui::Text("Scale");
		ImGui::SameLine(80.0f);
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::DragFloat("##pp_scale", &selected->scale, 0.01f, 0.0001f, 10000.0f);

		glm::vec3 euler = glm::degrees(glm::eulerAngles(selected->rot));
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Rotation");
		ImGui::SameLine(80.0f);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat3("##pp_rot", &euler.x, 0.5f))
			selected->rot = glm::quat(glm::radians(euler));
	}

	if (RenderComponent* rc = getComponent<RenderComponent>(selected))
	{
		if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const RenderComponent::SpawnInfo* info = getRenderSpawnInfo(selected);

			ImGui::AlignTextToFramePadding();
			ImGui::Text("Container");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::TextWrapped("%s", (info && info->container) ? info->container->getFilePath().c_str() : "<none>");

			ImGui::AlignTextToFramePadding();
			ImGui::Text("Node");
			ImGui::SameLine(100.0f);
			ImGui::TextWrapped("%s", info ? info->nodePath.c_str() : "<none>");

			if (info)
			{
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Position");
				ImGui::SameLine(100.0f);
				ImGui::Text("%.3f, %.3f, %.3f", info->localTransform.pos.x, info->localTransform.pos.y, info->localTransform.pos.z);

				ImGui::AlignTextToFramePadding();
				ImGui::Text("Scale");
				ImGui::SameLine(100.0f);
				ImGui::Text("%.3f", info->localTransform.scale);

				glm::vec3 euler = glm::degrees(glm::eulerAngles(info->localTransform.quat));
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Rotation");
				ImGui::SameLine(100.0f);
				ImGui::Text("%.2f, %.2f, %.2f", euler.x, euler.y, euler.z);
			}

			ImGui::AlignTextToFramePadding();
			ImGui::Text("Mesh insts");
			ImGui::SameLine(100.0f);
			ImGui::Text("%zu", rc->node.getNumMeshInstances());

			ImGui::AlignTextToFramePadding();
			ImGui::Text("Show bounds");
			ImGui::SameLine(100.0f);
			ImGui::Checkbox("##pp_show_bounds", &rc->showBounds);
		}
	}

	if (ScriptComponent* script = getComponent<ScriptComponent>(selected))
	{
		if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Script");
			/*
			ImGui::SameLine(80.0f);
			ImGui::SetNextItemWidth(-1.0f);
			char scriptBuf[256];
			strncpy_s(scriptBuf, sizeof(scriptBuf), script->scriptPath.c_str(), sizeof(scriptBuf) - 1);
			scriptBuf[sizeof(scriptBuf) - 1] = '\0';
			if (ImGui::InputText("##pp_script", scriptBuf, sizeof(scriptBuf), ImGuiInputTextFlags_EnterReturnsTrue))
				script->scriptPath = scriptBuf;
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FILE"))
					script->scriptPath = static_cast<const char*>(payload->Data);
				ImGui::EndDragDropTarget();
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Type a path or drag a .scr here");
			ImGui::AlignTextToFramePadding();*/
			ImGui::Text("Enabled");
			ImGui::SameLine(80.0f);
			ImGui::Checkbox("##pp_script_enabled", &script->enabled);
		}
	}
}
