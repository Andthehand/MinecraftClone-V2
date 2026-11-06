#pragma once

#include <glm/vec3.hpp>

class Camera {
public:
	Camera();

	void SetPosition(glm::vec3 position);
	void MoveLocal(float forward, float right, float up);
	void MoveWorld(glm::vec3 delta);
	void AddYawPitch(float yawDeltaRadians, float pitchDeltaRadians);

	[[nodiscard]] glm::vec3 GetPosition() const;
	[[nodiscard]] glm::vec3 GetForward() const;
	[[nodiscard]] glm::vec3 GetRight() const;
	[[nodiscard]] glm::vec3 GetUp() const;
	[[nodiscard]] float GetFovYRadians() const;

private:
	glm::vec3 m_Position;
	float m_YawRadians;
	float m_PitchRadians;
	float m_FovYRadians;
};
