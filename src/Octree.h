#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

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
	std::uint32_t GetVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept {
		const Node* node = &m_Root;
		std::size_t bit = m_Depth;
		while (node->children) {
			--bit;
			const auto child = ((x >> bit) & 1u)
				| (((y >> bit) & 1u) << 1) | (((z >> bit) & 1u) << 2);
			node = &(*node->children)[child];
		}
		return node->voxel;
	}

	// Copy the entire volume in X-fastest order: (z * side + y) * side + x.
	// The destination must hold at least side^3 voxels; extra entries are untouched.
	void CopyVoxels(std::span<std::uint32_t> destination) const;
	void SetVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t voxel);

	uint32_t GetSideLength() const noexcept { return m_SideLength; }

	void RemoveVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z);
	void Fill(std::uint32_t voxel);
	bool IsUniform() const noexcept { return !m_Root.children; }
private:
	// Terrain construction assigns uniform regions directly instead of editing
	// individual voxels through the root for every column.
	friend class TerrainGenerator;

	static std::uint32_t ChildIndex(std::uint32_t x, std::uint32_t y,
		std::uint32_t z, std::size_t bit) noexcept;

	static void SetNode(Node& node, std::uint32_t x, std::uint32_t y,
		std::uint32_t z, std::size_t remaining, std::uint32_t voxel);
	static void CopyNode(const Node& node, std::uint32_t* destination,
		std::size_t stride, std::uint32_t size);
private:
	const uint32_t m_Depth;
	const uint32_t m_SideLength;

	Node m_Root;
};
