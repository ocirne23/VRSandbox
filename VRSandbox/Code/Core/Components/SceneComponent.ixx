export module Components.SceneComponent;

export namespace Ogre { class SceneNode; }

export struct SceneComponent
{
	Ogre::SceneNode* pNode = nullptr;
};