module;

export module Components.GraphicsComponent;

export namespace Ogre { class Item; }

export struct GraphicsComponent
{
	Ogre::Item* pItem = nullptr;
};