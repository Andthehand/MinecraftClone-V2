#pragma once

struct Vec3 {
	float x;
	float y;
	float z;
};

class Camera {
public:
	Camera();

	void SetPosition(Vec3 position);
	void MoveLocal(float forward, float right, float up);
	void MoveWorld(Vec3 delta);
	void AddYawPitch(float yawDeltaRadians, float pitchDeltaRadians);

	[[nodiscard]] Vec3 GetPosition() const;
	[[nodiscard]] Vec3 GetForward() const;
	[[nodiscard]] Vec3 GetRight() const;
	[[nodiscard]] Vec3 GetUp() const;
	[[nodiscard]] float GetFovYRadians() const;

private:
	Vec3 m_Position;
	float m_YawRadians;
	float m_PitchRadians;
	float m_FovYRadians;
};
