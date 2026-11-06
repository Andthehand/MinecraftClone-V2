#include "Camera.h"
#include "Options.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/ext/matrix_transform.hpp>

Camera::Camera()
	: m_Position(Options::Camera::START_POSITION)
	, m_YawRadians(glm::radians(Options::Camera::START_YAW_DEGREES))
	, m_PitchRadians(glm::radians(Options::Camera::START_PITCH_DEGREES))
	, m_FovYRadians(glm::radians(Options::Camera::FOV_Y_DEGREES)) {
}

void Camera::SetPosition(glm::vec3 position) {
	m_Position = position;
}

void Camera::MoveLocal(float forward, float right, float up) {
	const glm::vec3 forwardVector = GetForward();
	const glm::vec3 rightVector = GetRight();
	const glm::vec3 worldUp{ 0.0f, 1.0f, 0.0f };

	m_Position += forwardVector * forward + rightVector * right + worldUp * up;
}

void Camera::MoveWorld(glm::vec3 delta) {
	m_Position += delta;
}

void Camera::AddYawPitch(float yawDeltaRadians, float pitchDeltaRadians) {
	m_YawRadians += yawDeltaRadians;
	m_PitchRadians += pitchDeltaRadians;

	const float pitchLimit = glm::half_pi<float>() - Options::Camera::PITCH_LIMIT_MARGIN_RADIANS;
	m_PitchRadians = glm::clamp(m_PitchRadians, -pitchLimit, pitchLimit);
}

glm::vec3 Camera::GetPosition() const {
	return m_Position;
}

glm::vec3 Camera::GetForward() const {
	const float cosPitch = glm::cos(m_PitchRadians);
	return glm::normalize(glm::vec3{
		cosPitch * glm::cos(m_YawRadians),
		glm::sin(m_PitchRadians),
		cosPitch * glm::sin(m_YawRadians),
	});
}

glm::vec3 Camera::GetRight() const {
	const glm::vec3 worldUp{ 0.0f, 1.0f, 0.0f };
	return glm::normalize(glm::cross(GetForward(), worldUp));
}

glm::vec3 Camera::GetUp() const {
	return glm::normalize(glm::cross(GetRight(), GetForward()));
}

float Camera::GetFovYRadians() const {
	return m_FovYRadians;
}

glm::mat4 Camera::GetViewMatrix() const {
	// World forward remains +Z initially; visible points have negative view-space Z.
	return glm::lookAtRH(m_Position, m_Position + GetForward(), GetUp());
}
