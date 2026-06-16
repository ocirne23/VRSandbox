module Entity.Registry;

import Core;
import Core.Transform;
import Entity.Component;

void EntityRegistry::unregisterEntity(Entity* entity)
{
    // Order doesn't matter, so swap-and-pop.
    auto it = std::find(m_all.begin(), m_all.end(), entity);
    if (it != m_all.end())
    {
        *it = m_all.back();
        m_all.pop_back();
    }
}

Entity* EntityRegistry::getWorldRoot()
{
    if (!m_world)
    {
        m_world = createEntity(uint16(1 << EComponentID_Scene), Transform());
        m_world->name = "World";
    }
    return m_world.get();
}
