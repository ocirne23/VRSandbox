
import Core;
import Input;

void FreeFlyCameraController::initialize(glm::vec3 position, glm::vec3 lookAt, glm::vec3 up)
{
	m_position = position;
	m_direction = glm::normalize(lookAt - position);
	m_up = up;
	m_viewMatrix = glm::lookAt(position, lookAt, up);
}

void FreeFlyCameraController::update(float deltaTime)
{
	m_viewMatrix = glm::lookAt(m_position, m_position + m_direction, m_up);

	
}