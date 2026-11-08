#include "Octree.h"
#include <algorithm>

Octree::Octree(uint32_t depth) 
	: m_Depth(depth), m_SideLength(std::uint32_t{ 1 } << depth) {}

void Octree::CopyVoxels(std::span<std::uint32_t> destination) const {
	// Divide to validate even volumes whose voxel count would overflow size_t.
	CopyNode(m_Root, destination.data(), m_SideLength, m_SideLength);
}

void Octree::CopyNode(const Node& node, std::uint32_t* destination,
	std::size_t stride, std::uint32_t size) {
	if (!node.children) {
		for (std::uint32_t z = 0; z < size; ++z)
			for (std::uint32_t y = 0; y < size; ++y)
				std::fill_n(destination + (z * stride + y) * stride, size, node.voxel);
		return;
	}
	const auto half = size / 2;
	for (std::uint32_t child = 0; child < 8; ++child) {
		const auto offset = (child & 1u) * half
			+ ((child >> 1) & 1u) * half * stride
			+ ((child >> 2) & 1u) * half * stride * stride;
		CopyNode((*node.children)[child], destination + offset, stride, half);
	}
}

void Octree::SetVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t voxel) {
	SetNode(m_Root, x, y, z, m_Depth, voxel);
}

void Octree::RemoveVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
	SetVoxel(x, y, z, 0);
}

std::uint32_t Octree::ChildIndex(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::size_t bit) noexcept {
	return ((x >> bit) & 1u)
		| (((y >> bit) & 1u) << 1)
		| (((z >> bit) & 1u) << 2);
}

void Octree::SetNode(Node& node, std::uint32_t x, std::uint32_t y,
	std::uint32_t z, std::size_t remaining, std::uint32_t voxel) {
	if (!node.children && node.voxel == voxel)
		return;
	if (remaining == 0) {
		node.voxel = voxel;
		return;
	}
	if (!node.children) {
		node.children = std::make_unique<std::array<Node, 8>>();
		for (auto& child : *node.children)
			child.voxel = node.voxel;
	}
	SetNode((*node.children)[ChildIndex(x, y, z, remaining - 1)], x, y, z, remaining - 1, voxel);
	const auto material = (*node.children)[0].voxel;
	for (const auto& child : *node.children)
		if (child.children || child.voxel != material)
			return;
	// All children agree: collapse them back into one uniform leaf.
	node.voxel = material;
	node.children.reset();
}

void Octree::Fill(std::uint32_t voxel) {
	m_Root.children.reset();
	m_Root.voxel = voxel;
}
