export module Entity:Prefab;

import Core;
import :Entity;

export bool savePrefab(Entity* root, const std::string& path);

// True if saving `root` as prefab `id` would produce a cycle (a descendant is a prefab instance of `id`).
export bool prefabWouldCycle(Entity* root, const std::string& id);

// Serializes `root` to prefab text (the same format savePrefab() writes to disk) without touching the
// filesystem — used to build/compare in-memory dirty-tracking baselines (e.g. the Prefab Editor UI).
export std::string serializePrefabText(Entity* root, const std::string& id);
