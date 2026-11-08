#include "Camera.h"
#include "Options.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

Camera::Camera()
	: m_Position(Options::Camera::START_POSITION)
	, m_YawRadians(glm::radians(Options::Camera::START_YAW_DEGREES))
	, m_PitchRadians(glm::radians(Options::Camera::START_PITCH_DEGREES))
	, m_FovYRadians(glm::radians(Options::Camera::VERTICAL_FOV_DEGREES))
	, m_AspectRatio(float(Options::Application::WINDOW_WIDTH) / float(Options::Application::WINDOW_HEIGHT)) {
}

void Camera::SetPosition(glm::vec3 position) {
	m_Position = position;
	m_ViewProjectionDirty = true;
}

void Camera::MoveLocal(float forward, float right, float up) {
	const glm::vec3 forwardVector = GetForward();
	const glm::vec3 rightVector = GetRight();
	const glm::vec3 worldUp{ 0.0f, 1.0f, 0.0f };

	m_Position += forwardVector * forward + rightVector * right + worldUp * up;
	m_ViewProjectionDirty = true;
}

void Camera::MoveWorld(glm::vec3 delta) {
	m_Position += delta;
	m_ViewProjectionDirty = true;
}

void Camera::AddYawPitch(float yawDeltaRadians, float pitchDeltaRadians) {
	m_YawRadians += yawDeltaRadians;
	m_PitchRadians += pitchDeltaRadians;

	const float pitchLimit = glm::half_pi<float>() - Options::Camera::PITCH_LIMIT_MARGIN_RADIANS;
	m_PitchRadians = glm::clamp(m_PitchRadians, -pitchLimit, pitchLimit);
	m_ViewProjectionDirty = true;
}

void Camera::SetAspectRatio(float aspectRatio) {
	if (aspectRatio > 0.0f && aspectRatio != m_AspectRatio) {
		m_AspectRatio = aspectRatio;
		m_ViewProjectionDirty = true;
	}
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

glm::mat4 Camera::GetProjectionMatrix() const {
	auto projection = glm::perspectiveRH_ZO(m_FovYRadians, m_AspectRatio,
		Options::Camera::NEAR_CLIP_DISTANCE, Options::Camera::FAR_CLIP_DISTANCE);
	// Vulkan uses [0, 1] depth and a positive-height viewport.
	projection[1][1] *= -1.0f;
	
	return projection;
}

const glm::mat4& Camera::GetViewProjectionMatrix() const {
	if (m_ViewProjectionDirty) {
		m_ViewProjection = GetProjectionMatrix() * GetViewMatrix();
		m_ViewProjectionDirty = false;
	}
	return m_ViewProjection;
}

std::array<glm::vec4, 6> Camera::GetFrustumPlanes() const {
	const glm::mat4& viewProjection = GetViewProjectionMatrix();
	const auto row = [&viewProjection](int i) {
		return glm::vec4(viewProjection[0][i], viewProjection[1][i],
			viewProjection[2][i], viewProjection[3][i]);
	};

	// Vulkan clip space: -w <= x,y <= w, 0 <= z <= w.
	std::array<glm::vec4, 6> planes = {row(3) + row(0), row(3) - row(0),
		row(3) + row(1), row(3) - row(1), row(2), row(3) - row(2)};
	for (auto& plane : planes) {
		const float length = glm::length(glm::vec3(plane));
		if (length > 0.0f) {
			plane /= length;
		}
	}
	return planes;
}

bool Camera::IsBoxVisible(const std::array<glm::vec4, 6>& planes,
	const glm::vec3& minimum, const glm::vec3& maximum) {
	const glm::vec3 center = (minimum + maximum) * 0.5f;
	const glm::vec3 extent = (maximum - minimum) * 0.5f;
	for (const auto& plane : planes) {
		const glm::vec3 normal(plane);
		const float distance = glm::dot(normal, center) + plane.w;
		const float radius = glm::dot(glm::abs(normal), extent);
		// Keep touching boxes and a small world-space margin to avoid edge flicker.
		if (distance + radius < -0.001f) {
			return false;
		}
	}

	return true;
}
