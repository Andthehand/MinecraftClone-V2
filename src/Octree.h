#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

struct Node {
	std::array<std::unique_ptr<Node>, 8> children;
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
private:
	static std::uint32_t ChildIndex(std::uint32_t x, std::uint32_t y,
		std::uint32_t z, std::size_t bit) noexcept;

	// Returns true when this subtree is empty, allowing its parent to prune it.
	static bool EraseNode(Node& node, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::size_t remaining);
private:
	const uint32_t m_Depth;
	const uint32_t SideLength;

	Node m_Root;
};
