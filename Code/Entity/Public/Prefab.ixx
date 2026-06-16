export module Entity.Prefab;

import Core;
import Entity;

// Prefab (.pre) serialization. Saving is self-contained (an entity subtree -> text) and lives here so
// any library that can see the Entity lib (e.g. the UI) can save a hierarchy. LOADING a prefab lives
// in Scene::World instead, because re-instantiating an entity's source ".ent" (its mesh/RenderNode)
// needs the World's ObjectContainers.

// Serializes `root` and its SceneComponent children to a .pre text file at `path` (an absolute path).
// Returns false if the file could not be written.
export bool savePrefab(Entity* root, const std::string& path);
