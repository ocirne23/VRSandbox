export module Entity.Component;

import Entity;
import Core;
import Core.glm;
import RendererVK;

constexpr int MaxInlineComponentTypes = 3;
enum EComponentID : uint16
{
    // inline components
    EComponentID_Zone = 0,
    EComponentID_Cull = 1,
    EComponentID_Render = 2,
    // ecs components
    EComponentID_GameLogic = 3,
};

struct ZoneComponent
{
    static constexpr EComponentID getId() { return EComponentID_Zone; };
    std::vector<EntityPtr> entities;
};

struct CullingComponent
{
    static constexpr EComponentID getId() { return EComponentID_Cull; };
    float radius;
    glm::vec3 center;
    glm::vec3 extent;
};

struct RenderComponent
{
    static constexpr EComponentID getId() { return EComponentID_Render; };
    std::vector<RenderNode> renderNodes;
};

struct GameLogicComponent
{
    static constexpr EComponentID getId() { return EComponentID_GameLogic; };
    float foo;
    bool bar;
    int awa;
};

struct ECSComponent
{
    uint32 idx;
    EComponentID id;
    uint16 gen;

    EComponentID getId() const { return id; }

    template <typename T>
    T* getComponent() { return nullptr; }

    template<GameLogicComponent>
    GameLogicComponent* getComponent();
};

template<GameLogicComponent>
GameLogicComponent* ECSComponent::getComponent()
{
    return nullptr;
}

static constexpr std::array<uint32, MaxInlineComponentTypes> g_inlineComponents {
    sizeof(ZoneComponent),
    sizeof(CullingComponent),
    sizeof(RenderComponent)
};

template <typename T>
T* Entity::getComponent()
{
    constexpr EComponentID id = T::getId();
    if constexpr (id < MaxInlineComponentTypes)
    {
        return reinterpret_cast<T*>(reinterpret_cast<uint8*>(this) + getComponentOffset(typeBits, id));
    }
    else
    {
        ECSComponent* pECSComponent = reinterpret_cast<ECSComponent*>(reinterpret_cast<uint8*>(this) + getComponentOffset(typeBits, id));
        return pECSComponent->getComponent<T>();
    }
}

constexpr uint32 Entity::getComponentOffset(uint16 compTypeBits, EComponentID id)
{
    uint32 offset = 0;
    for (uint16 i = 0; i < id; ++i)
    {
        if (i < MaxInlineComponentTypes && compTypeBits & (1 << i))
        {
            offset += g_inlineComponents[i];
        }
        if (i >= MaxInlineComponentTypes)
        {
            offset += sizeof(ECSComponent);
        }
    }
    return offset;
}
