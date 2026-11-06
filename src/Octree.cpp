#include "Octree.h"

Octree::Octree(uint32_t depth) 
	: m_Depth(depth), SideLength(std::uint32_t{ 1 } << depth) {}

std::uint32_t Octree::GetVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
	const Node* node = &m_Root;
	for (std::size_t remaining = m_Depth; remaining > 0; --remaining) {
		node = node->children[ChildIndex(x, y, z, remaining - 1)].get();
		if (!node)
			return 0;
	}

	return node->voxel;
}

void Octree::SetVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t voxel) {
	if (voxel == 0) {
		EraseNode(m_Root, x, y, z, m_Depth);
		return;
	}

	Node* node = &m_Root;
	for (std::size_t remaining = m_Depth; remaining > 0; --remaining) {
		auto& child = node->children[ChildIndex(x, y, z, remaining - 1)];
		if (!child)
			child = std::make_unique<Node>();
		node = child.get();
	}
	node->voxel = voxel;
}

void Octree::RemoveVoxel(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
	SetVoxel(x, y, z, 0);
}

std::uint32_t Octree::ChildIndex(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::size_t bit) noexcept {
	return ((x >> bit) & 1u)
		| (((y >> bit) & 1u) << 1)
		| (((z >> bit) & 1u) << 2);
}

bool Octree::EraseNode(Node& node, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::size_t remaining) {
	if (remaining == 0) {
		node.voxel = 0;
		return true;
	}
	auto& child = node.children[ChildIndex(x, y, z, remaining - 1)];
	if (child && EraseNode(*child, x, y, z, remaining - 1))
		child.reset();
	for (const auto& sibling : node.children)
		if (sibling)
			return false;
	return true;
}
