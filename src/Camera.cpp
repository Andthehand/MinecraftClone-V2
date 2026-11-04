#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float PI = 3.14159265358979323846f;
	constexpr float HALF_PI = PI * 0.5f;
	constexpr float DEG_TO_RAD = PI / 180.0f;

	float Dot(Vec3 a, Vec3 b) {
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	Vec3 Normalize(Vec3 value) {
		const float lengthSquared = Dot(value, value);
		if (lengthSquared <= 0.0f) {
			return Vec3{ 0.0f, 0.0f, 0.0f };
		}
		const float invLength = 1.0f / std::sqrt(lengthSquared);
		return Vec3{ value.x * invLength, value.y * invLength, value.z * invLength };
	}

	Vec3 Cross(Vec3 a, Vec3 b) {
		return Vec3{
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x,
		};
	}
}

Camera::Camera()
	: m_Position{ 0.0f, 8.0f, -12.0f }
	, m_YawRadians(PI * 0.5f)
	, m_PitchRadians(0.0f)
	, m_FovYRadians(70.0f * DEG_TO_RAD) {
}

void Camera::SetPosition(Vec3 position) {
	m_Position = position;
}

void Camera::MoveLocal(float forward, float right, float up) {
	const Vec3 forwardVector = GetForward();
	const Vec3 rightVector = GetRight();
	const Vec3 worldUp{ 0.0f, 1.0f, 0.0f };

	m_Position.x += forwardVector.x * forward + rightVector.x * right + worldUp.x * up;
	m_Position.y += forwardVector.y * forward + rightVector.y * right + worldUp.y * up;
	m_Position.z += forwardVector.z * forward + rightVector.z * right + worldUp.z * up;
}

void Camera::MoveWorld(Vec3 delta) {
	m_Position.x += delta.x;
	m_Position.y += delta.y;
	m_Position.z += delta.z;
}

void Camera::AddYawPitch(float yawDeltaRadians, float pitchDeltaRadians) {
	m_YawRadians += yawDeltaRadians;
	m_PitchRadians += pitchDeltaRadians;

	const float pitchLimit = HALF_PI - 0.01f;
	m_PitchRadians = std::clamp(m_PitchRadians, -pitchLimit, pitchLimit);
}

Vec3 Camera::GetPosition() const {
	return m_Position;
}

Vec3 Camera::GetForward() const {
	const float cosPitch = std::cos(m_PitchRadians);
	return Normalize(Vec3{
		cosPitch * std::cos(m_YawRadians),
		std::sin(m_PitchRadians),
		cosPitch * std::sin(m_YawRadians),
	});
}

Vec3 Camera::GetRight() const {
	const Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
	return Normalize(Cross(GetForward(), worldUp));
}

Vec3 Camera::GetUp() const {
	return Normalize(Cross(GetRight(), GetForward()));
}

float Camera::GetFovYRadians() const {
	return m_FovYRadians;
}
