module;

#include <glm/gtc/matrix_access.hpp>

export module Core.Frustum;

import Core;


export struct Frustum
{
	glm::vec4 planes[6];
	void fromMatrix(const glm::mat4& matrix)
	{
		planes[0] = glm::row(matrix, 3) + glm::row(matrix, 0);
		planes[1] = glm::row(matrix, 3) - glm::row(matrix, 0);
		planes[2] = glm::row(matrix, 3) + glm::row(matrix, 1);
		planes[3] = glm::row(matrix, 3) - glm::row(matrix, 1);
		planes[4] = glm::row(matrix, 3) + glm::row(matrix, 2);
		planes[5] = glm::row(matrix, 3) - glm::row(matrix, 2);
		for (int i = 0; i < 6; i++)
		{
			planes[i] = glm::normalize(planes[i]);
		}
	}
};