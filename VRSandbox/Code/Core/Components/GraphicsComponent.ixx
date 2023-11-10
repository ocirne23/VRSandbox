export module Components.GraphicsComponent;

export namespace Ogre { class Item; class Vector3; class Quaternion; }

export struct GraphicsComponent
{
	Ogre::Item* pItem = nullptr;

    void setOffset(const Ogre::Vector3& offset);
    void setScale(const Ogre::Vector3& scale);
    void setRotation(const Ogre::Quaternion& rot);
};