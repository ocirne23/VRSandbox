export module RendererVK.ObjectContainer;

import Core;
import RendererVK.Mesh;

export class SceneData;

export class ObjectContainer final
{
public:

    ObjectContainer() {}
    ~ObjectContainer() {}
    ObjectContainer(const ObjectContainer&) = delete;

    bool initialize(SceneData& sceneData);

private:

    std::vector<Mesh> m_meshes;
};