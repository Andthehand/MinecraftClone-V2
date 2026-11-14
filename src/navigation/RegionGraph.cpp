#include "navigation/RegionGraph.h"
#include "diagnostics/Profiling.h"
#include "world/Block.h"
#include <algorithm>
#include <map>
#include <queue>
#include <tuple>

namespace Navigation::detail {
void AssignStrongComponents(NavigationRegion& region) {
    MC_PROFILE_ZONE("Build navigation components");

    const auto nodeCount = region.nodes.size();
    std::vector<bool> visitedNodes(nodeCount, false);
    std::vector<int> finishOrder;
    std::vector<std::pair<int, int>> depthFirstStack;
    std::vector<std::vector<int>> incomingEdges(nodeCount);

    for (int sourceIndex = 0; sourceIndex < int(nodeCount); ++sourceIndex) {
        for (int edgeIndex = 0; edgeIndex < region.nodes[sourceIndex].edgeCount; ++edgeIndex) {
            const int destinationIndex = region.nodes[sourceIndex].edges[edgeIndex].targetNodeIndex;

            if (destinationIndex >= 0) {
                incomingEdges[destinationIndex].push_back(sourceIndex);
            }
        }
    }

    for (int rootIndex = 0; rootIndex < int(nodeCount); ++rootIndex) {
        if (visitedNodes[rootIndex]) {
            continue;
        }

        visitedNodes[rootIndex] = true;
        depthFirstStack.emplace_back(rootIndex, 0);

        while (!depthFirstStack.empty()) {
            auto& [nodeIndex, nextEdgeIndex] = depthFirstStack.back();
            const auto& sourceNode = region.nodes[nodeIndex];

            if (nextEdgeIndex == sourceNode.edgeCount) {
                finishOrder.push_back(nodeIndex);
                depthFirstStack.pop_back();
                continue;
            }

            const int destinationIndex = sourceNode.edges[nextEdgeIndex++].targetNodeIndex;

            if (destinationIndex < 0 || visitedNodes[destinationIndex]) {
                continue;
            }

            visitedNodes[destinationIndex] = true;
            depthFirstStack.emplace_back(destinationIndex, 0);
        }
    }

    int componentIndex = 0;
    std::vector<int> pendingNodes;

    for (auto entry = finishOrder.rbegin(); entry != finishOrder.rend(); ++entry) {
        if (region.nodes[*entry].componentIndex >= 0) {
            continue;
        }

        region.nodes[*entry].componentIndex = componentIndex;
        pendingNodes.push_back(*entry);

        while (!pendingNodes.empty()) {
            const int nodeIndex = pendingNodes.back();
            pendingNodes.pop_back();

            for (int previous : incomingEdges[nodeIndex]) {
                if (region.nodes[previous].componentIndex >= 0) {
                    continue;
                }

                region.nodes[previous].componentIndex = componentIndex;
                pendingNodes.push_back(previous);
            }
        }

        ++componentIndex;
    }
}
} // namespace Navigation::detail

namespace Navigation {
using namespace detail;

struct RegionGraph::VoxelSamples {
    static constexpr int Side = ClusterSide + 2;

    enum State : uint8_t { Unknown, Air, Solid };

    BlockPosition origin;
    int minimumY;
    int maximumY;
    std::vector<uint8_t> voxels;

    VoxelSamples(BlockPosition position, int minY, int maxY) : origin(position), minimumY(minY), maximumY(maxY) {}

    int Height() const { return maximumY - minimumY; }

    size_t Index(BlockPosition localPosition) const {
        return ((localPosition.z + 1) * Height() + localPosition.y - minimumY) * Side + localPosition.x + 1;
    }

    bool IsAir(BlockPosition position) const { return voxels[Index(position)] == Air; }

    bool IsSolid(BlockPosition position) const { return voxels[Index(position)] == Solid; }
};

bool RegionGraph::SampleRegionVoxels(VoxelSamples& samples, SearchBudget& budget,
                                     const SnapshotLookup& findSnapshot) const {
    const BlockPosition origin = samples.origin;

    // Sample a voxel halo once. Subsequent searches use cached adjacency,
    // avoiding repeated world hash lookups and octree walks for every edge.
    constexpr int side = VoxelSamples::Side;
    const int minY = samples.minimumY;
    const int maxY = samples.maximumY;
    const int sizeY = maxY - minY;
    constexpr int chunkSide = Options::World::CHUNK_SIZE;

    const auto chunkOf = [](BlockPosition position) {
        return BlockPosition(FloorDivide(position.x, chunkSide), FloorDivide(position.y, chunkSide),
                             FloorDivide(position.z, chunkSide));
    };

    const auto baseChunk = chunkOf(origin);
    const auto first = chunkOf(origin + BlockPosition(-1, minY, -1));
    const auto last = chunkOf(origin + BlockPosition(ClusterSide, maxY - 1, ClusterSide));
    std::array<std::shared_ptr<ChunkSnapshot>, 27> sampledChunks;

    const auto chunkSlot = [&](BlockPosition chunk) {
        const auto difference = chunk - baseChunk + BlockPosition(1);

        return (difference.z * 3 + difference.y) * 3 + difference.x;
    };

    // Sample all chunks in the region and its immediate neighbors.
    for (int z = first.z; z <= last.z; ++z) {
        for (int y = first.y; y <= last.y; ++y) {
            for (int x = first.x; x <= last.x; ++x) {
                const BlockPosition chunk{x, y, z};

                if (budget.HasTimeLimitExpired(true)) {
                    return false;
                }

                sampledChunks[chunkSlot(chunk)] = findSnapshot(chunk);
            }
        }
    }

    samples.voxels.resize(side * sizeY * side);

    for (int z = -1; z <= ClusterSide; ++z) {
        for (int y = minY; y < maxY; ++y) {
            for (int x = -1; x <= ClusterSide; ++x) {
                if (budget.HasTimeLimitExpired()) {
                    return false;
                }

                const BlockPosition position{x, y, z};
                const auto worldCell = origin + position;
                const auto chunk = chunkOf(worldCell);
                const auto& snapshot = sampledChunks[chunkSlot(chunk)];

                if (!snapshot) {
                    continue;
                }

                const auto local = worldCell - chunk * chunkSide;
                const auto block = snapshot->voxels[(local.z * chunkSide + local.y) * chunkSide + local.x];
                samples.voxels[samples.Index(position)] =
                    block == Blocks::Air ? VoxelSamples::Air : VoxelSamples::Solid;
            }
        }
    }

    return true;
}

bool RegionGraph::CollectWalkableNodes(NavigationRegion& region, const VoxelSamples& samples,
                                       SearchBudget& budget) const {
    const auto air = [&](BlockPosition position) { return samples.IsAir(position); };
    const auto solid = [&](BlockPosition position) { return samples.IsSolid(position); };

    for (int index = 0; index < ClusterVolume; ++index) {
        if (budget.HasTimeLimitExpired()) {
            return false;
        }

        const auto position = PositionFromCellIndex(index, {0, 0, 0});

        if (!m_Movement.CanStandUsing(position, air, solid)) {
            continue;
        }

        if (!budget.TrySpendNode()) {
            return false;
        }

        region.nodeIndexAtCell[index] = int(region.nodes.size());
        region.nodes.push_back({.cellIndex = index});
    }

    return true;
}

bool RegionGraph::ConnectWalkableNodes(NavigationRegion& region, const VoxelSamples& samples,
                                       SearchBudget& budget) const {
    const auto& agent = m_Movement.GetAgent();
    const auto air = [&](BlockPosition position) { return samples.IsAir(position); };
    const auto solid = [&](BlockPosition position) { return samples.IsSolid(position); };
    const BlockPosition origin = region.coordinates * ClusterSide;

    for (auto& node : region.nodes) {
        if (budget.HasTimeLimitExpired()) {
            return false;
        }

        const auto from = PositionFromCellIndex(node.cellIndex, {0, 0, 0});

        for (auto direction : Directions) {
            for (int heightChange = agent.maxStepUp; heightChange >= -agent.maxDrop; --heightChange) {
                const auto destination = from + direction + BlockPosition(0, heightChange, 0);

                if (!m_Movement.CanStandUsing(destination, air, solid) ||
                    !m_Movement.HasMovementClearance(from, destination, air)) {
                    continue;
                }

                const auto target = origin + destination;
                const int local = ClusterForPosition(target) == region.coordinates
                                      ? region.nodeIndexAtCell[LocalCellIndex(target, region.coordinates)]
                                      : -1;
                node.edges[node.edgeCount++] = {target, local, 1 + std::abs(heightChange)};
                break;
            }
        }
    }

    return true;
}

bool RegionGraph::Build(NavigationRegion& region, SearchBudget& budget, const SnapshotLookup& findSnapshot) const {
    if (!m_Movement.IsValid()) {
        budget.result.status = PathStatus::InvalidAgent;

        return false;
    }

    const auto& agent = m_Movement.GetAgent();
    VoxelSamples samples(region.coordinates * ClusterSide, -agent.maxDrop - 1,
                         ClusterSide + agent.height + agent.maxStepUp);

    if (!SampleRegionVoxels(samples, budget, findSnapshot) || !CollectWalkableNodes(region, samples, budget) ||
        !ConnectWalkableNodes(region, samples, budget)) {
        return false;
    }

    if (budget.HasTimeLimitExpired(true)) {
        return false;
    }

    AssignStrongComponents(region);

    return true;
}

bool RegionGraph::BuildEntrances(NavigationRegion& region, SearchBudget& budget, const RegionLookup& findRegion) const {
    if (region.entrancesBuilt) {
        return true;
    }

    MC_PROFILE_ZONE("Build sparse navigation entrances");

    // A representative per entrance patch AND endpoint SCC pair. Keeping
    // separate component pairs preserves reachability through directed drops.
    struct EntranceKey {
        int sourceComponent;
        BlockPosition destinationCluster;
        int destinationComponent;
        BlockPosition patch;

        bool operator<(const EntranceKey& other) const {
            return std::tie(sourceComponent, destinationCluster.x, destinationCluster.y, destinationCluster.z,
                            destinationComponent, patch.x, patch.y, patch.z) <
                   std::tie(other.sourceComponent, other.destinationCluster.x, other.destinationCluster.y,
                            other.destinationCluster.z, other.destinationComponent, other.patch.x, other.patch.y,
                            other.patch.z);
        }
    };

    struct EntranceCandidate {
        RegionEntrance entrance;
        int distanceToPatchCenter;
    };

    std::map<EntranceKey, EntranceCandidate> entrances;
    PositionMap<std::shared_ptr<NavigationRegion>> neighbors;

    for (int sourceNodeIndex = 0; sourceNodeIndex < int(region.nodes.size()); ++sourceNodeIndex) {
        if (budget.HasTimeLimitExpired()) {
            return false;
        }

        const auto& node = region.nodes[sourceNodeIndex];
        const auto localPosition = PositionFromCellIndex(node.cellIndex, {0, 0, 0});

        for (int edgeIndex = 0; edgeIndex < node.edgeCount; ++edgeIndex) {
            const auto& edge = node.edges[edgeIndex];

            if (edge.targetNodeIndex >= 0) {
                continue;
            }

            const auto neighborCoordinates = ClusterForPosition(edge.target);
            auto& neighbor = neighbors[neighborCoordinates];

            if (!neighbor) {
                neighbor = findRegion(neighborCoordinates);
            }

            if (!neighbor) {
                return false;
            }

            const int destinationNodeIndex =
                neighbor->nodeIndexAtCell[LocalCellIndex(edge.target, neighborCoordinates)];

            if (destinationNodeIndex < 0) {
                continue;
            }

            const auto patch = localPosition / EntrancePatchSide;
            const EntranceKey key{node.componentIndex, neighborCoordinates,
                                  neighbor->nodes[destinationNodeIndex].componentIndex, patch};
            const int distanceToPatchCenter =
                ManhattanDistance(localPosition, patch * EntrancePatchSide + BlockPosition(EntrancePatchSide / 2));

            const auto entry = entrances.find(key);

            if (entry == entrances.end() || distanceToPatchCenter < entry->second.distanceToPatchCenter) {
                entrances.insert_or_assign(
                    key, EntranceCandidate{
                             .entrance = {.sourceNodeIndex = sourceNodeIndex, .target = edge.target, .cost = edge.cost},
                             .distanceToPatchCenter = distanceToPatchCenter,
                         });
            }
        }
    }

    for (const auto& [key, candidate] : entrances) {
        region.entrances.push_back(candidate.entrance);
    }

    region.entrancesBuilt = true;

    return true;
}

std::shared_ptr<RegionSearchTree> RegionGraph::BuildSearchTree(const NavigationRegion& region, int sourceNodeIndex,
                                                               SearchBudget& budget) const {
    MC_PROFILE_ZONE("Build navigation cluster tree");

    auto tree = std::make_shared<RegionSearchTree>(region.nodes.size());

    using DistanceQueueEntry = std::pair<int, int>;
    std::priority_queue<DistanceQueueEntry, std::vector<DistanceQueueEntry>, std::greater<>> open;

    tree->costFromSource[sourceNodeIndex] = 0;
    open.emplace(0, sourceNodeIndex);

    while (!open.empty()) {
        const auto [costFromSource, nodeIndex] = open.top();
        open.pop();

        if (budget.HasTimeLimitExpired()) {
            return {};
        }

        if (costFromSource != tree->costFromSource[nodeIndex]) {
            continue;
        }

        if (!budget.TrySpendNode()) {
            return {};
        }

        const auto& sourceNode = region.nodes[nodeIndex];

        for (int edgeIndex = 0; edgeIndex < sourceNode.edgeCount; ++edgeIndex) {
            const auto& edge = sourceNode.edges[edgeIndex];

            if (edge.targetNodeIndex < 0 || costFromSource + edge.cost >= tree->costFromSource[edge.targetNodeIndex]) {
                continue;
            }

            tree->costFromSource[edge.targetNodeIndex] = costFromSource + edge.cost;
            tree->parentNodeIndex[edge.targetNodeIndex] = nodeIndex;
            open.emplace(costFromSource + edge.cost, edge.targetNodeIndex);
        }
    }

    return tree;
}

} // namespace Navigation
