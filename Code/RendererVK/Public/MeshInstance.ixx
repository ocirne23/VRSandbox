export module RendererVK.MeshInstance;

import Core;
import Entity;

export class alignas(16) MeshInstance final
{
public:

    struct alignas(16) RenderLayout
    {
        glm::vec3 pos;
        float scale = 1.0f;
        glm::quat rot;
    };
    RenderLayout transform;

    uint32 getMeshIdx() const { return meshIdx; }

private:

    friend class Mesh;
    uint32 meshIdx;
};