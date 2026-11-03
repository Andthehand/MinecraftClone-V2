#include <array>
#include <memory>
#include <vector>
#include <bit>

struct Node {
	std::array<std::unique_ptr<Node>, 8> children;
	uint32_t voxel = 0;
};

/*
If internal
	Bits
	0-7		Child Mask
	8-31	First Child Index
If leaf
	Bits
	0-7		0
	8-31	Voxel ID
*/
struct GpuOctreeNode {
	std::uint32_t packed;

	[[nodiscard]] constexpr std::uint32_t childMask() const noexcept {
		return packed & 0xFFu;
	}
	[[nodiscard]] constexpr bool isLeaf() const noexcept {
		return childMask() == 0;
	}
	[[nodiscard]] constexpr std::uint32_t payload() const noexcept {
		return packed >> 8;
	}
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
	void Clear() noexcept { m_Root = Node{}; }

	[[nodiscard]] 
	std::vector<GpuOctreeNode> ToGpuNodes() const;
private:
	static std::uint32_t ChildIndex(std::uint32_t x, std::uint32_t y,
		std::uint32_t z, std::size_t bit) noexcept;

	// Returns true when this subtree is empty, allowing its parent to prune it.
	static bool EraseNode(Node& node, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::size_t remaining);
	static void FlattenNode(const Node& source, std::uint32_t destination, std::vector<GpuOctreeNode>& output);
private:
	const uint32_t m_Depth;
	const uint32_t SideLength;
	const uint32_t MaxVoxelId;
	const uint32_t MaxGpuNodes;

	Node m_Root;
};
