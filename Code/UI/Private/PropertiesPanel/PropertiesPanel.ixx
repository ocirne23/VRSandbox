export module UI:PropertiesPanel;

import Core;
import Entity;

export class PropertiesPanel
{
public:
	void render(Entity* selected);

private:
	// The script's exposed ScriptData fields, edited LIVE (never serialized -- the .pre holds the initial value,
	// authored in the Entity Editor). Reads the layout from the loaded module's field table, so it shows nothing
	// until the script has compiled.
	void drawScriptDataFields(ScriptComponent& script);
};
