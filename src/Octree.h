#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

struct Node {
	// A leaf represents an entire uniform region, including solid regions.
	// Allocate all eight children together only when that region is edited.
	std::unique_ptr<std::array<Node, 8>> children;
	uint32_t voxel = 0;
};

class Octree {
public:
	explicit Octree(uint32_t depth);
	~Octree() = default;
	Octree(const Octree&) = delete;
	Octree& operator=(const Octree&) = delete;

	[[nodiscard]]
	std::uint32_t GetVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z) const;
	void SetVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t voxel);

	uint32_t GetSideLength() const noexcept { return SideLength; }

	void RemoveVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z);
	void Fill(std::uint32_t voxel);
	bool IsUniform() const noexcept { return !m_Root.children; }
private:
	static std::uint32_t ChildIndex(std::uint32_t x, std::uint32_t y,
		std::uint32_t z, std::size_t bit) noexcept;

	static void SetNode(Node& node, std::uint32_t x, std::uint32_t y,
		std::uint32_t z, std::size_t remaining, std::uint32_t voxel);
private:
	const uint32_t m_Depth;
	const uint32_t SideLength;

	Node m_Root;
};
