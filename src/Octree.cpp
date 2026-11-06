#include "Octree.h"

Octree::Octree(uint32_t depth) 
	: m_Depth(depth), SideLength(std::uint32_t{ 1 } << depth) {}

std::uint32_t Octree::GetVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
	const Node* node = &m_Root;
	for (std::size_t remaining = m_Depth; remaining > 0 && node->children; --remaining)
		node = &(*node->children)[ChildIndex(x, y, z, remaining - 1)];

	return node->voxel;
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
