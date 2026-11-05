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

std::vector<GpuOctreeNode> Octree::ToGpuNodes() const {
	std::vector<GpuOctreeNode> output(1);
	FlattenNode(m_Root, 0, output);

	return output;
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

void Octree::FlattenNode(const Node& source, std::uint32_t destination, std::vector<GpuOctreeNode>& output) {
	std::uint32_t mask = 0;
	for (std::uint32_t i = 0; i < source.children.size(); ++i) {
		if (source.children[i]) {
			mask |= 1u << i;
		}
	}

	if (mask == 0) {
		output[destination].packed = source.voxel << Options::OctreeFormat::PAYLOAD_SHIFT;
		return;
	}

	const auto count = static_cast<std::size_t>(std::popcount(mask));
	const auto firstChild = static_cast<std::uint32_t>(output.size());
	// Allocate every immediate child BEFORE allocating descendants.
	output.resize(output.size() + count);
	output[destination].packed = (firstChild << Options::OctreeFormat::PAYLOAD_SHIFT) | mask;

	std::uint32_t offset = 0;
	for (std::uint32_t i = 0; i < source.children.size(); ++i) {
		if (source.children[i]) {
			FlattenNode(*source.children[i], firstChild + offset++, output);
		}
	}
	// Never hold vector element references across resize/recursion.
}
