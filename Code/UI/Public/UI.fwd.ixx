export module UI.fwd;

import Core;

export extern "C++"
{
	class UI;
	namespace NodeEditor
	{
		class Link;
		class Node;
		class Scene;
		struct Pin;
		enum EPinShape : uint8;
		enum EPinType : uint8;
		enum ENodeStyle : uint8;
	}
}