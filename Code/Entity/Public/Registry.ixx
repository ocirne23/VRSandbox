export module Entity.Registry;

import Core;
import Entity;

// Single global directory of every live entity. createEntity/destroyEntity keep `m_all` in sync so
// tooling (e.g. the editor scene panel) can enumerate the whole world. The registry also owns the one
// persistent "World" root entity — a SceneComponent entity whose children are the editable scene
// hierarchy, so any entity (scene or loose) can be parented under it. Non-scene ("loose") entities
// that aren't parented stay owned by whoever created them. Not thread-safe (entity lifetime is
// single-thread).
export class EntityRegistry final
{
public:

    // Every live entity, in no particular order (non-owning).
    const std::vector<Entity*>& getAll() const { return m_all; }

    // The persistent "World" root entity (created lazily on first access). It carries a SceneComponent,
    // so its children form the scene hierarchy and anything can be reparented under it.
    Entity* getWorldRoot();

    // ---- maintained by the Entity lifetime / scene-graph code --------------------------------------

    void registerEntity(Entity* entity) { m_all.push_back(entity); }
    void unregisterEntity(Entity* entity);

private:

    std::vector<Entity*> m_all;
    EntityPtr            m_world;
};

export namespace Globals
{
    // Constructed after Globals::entityAllocator (plain .CRT$XCU) so its owning scene-root handles are
    // released *before* the allocator's chunks are freed during static destruction.
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU3")
    EntityRegistry entityRegistry;
#pragma warning(default: 4075)
}
