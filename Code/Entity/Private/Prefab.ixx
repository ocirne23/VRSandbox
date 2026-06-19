export module Entity:Prefab;

import Core;
import :Entity;

export bool savePrefab(Entity* root, const std::string& path);

// True if saving `root` as prefab `id` would produce a cycle (a descendant is a prefab instance of `id`).
export bool prefabWouldCycle(Entity* root, const std::string& id);
