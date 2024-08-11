export module RendererVK.MeshInstance;

import Core;
import RendererVK.Layout;

export class MeshInstance final : private RendererVKLayout::MeshInstance
{
public:

    using RendererVKLayout::MeshInstance::pos;
    using RendererVKLayout::MeshInstance::scale;
    using RendererVKLayout::MeshInstance::quat;

    uint32 getMeshIdx() const { return meshInfoIdx; }

private:

    friend class ObjectContainer;
    using RendererVKLayout::MeshInstance::meshInfoIdx;
};

static_assert(sizeof(MeshInstance) == sizeof(RendererVKLayout::MeshInstance));