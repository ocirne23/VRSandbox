export module Core.AABB;

import Core;

export struct AABB final
{
	glm::vec3 min;
	glm::vec3 max;
};