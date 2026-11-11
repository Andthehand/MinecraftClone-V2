#include "navigation/HierarchicalPathfinder.h"
#include "diagnostics/Profiling.h"
#include "navigation/NavigationCache.h"
#include <algorithm>
#include <queue>

namespace Navigation::detail {
struct HierarchyRecord {
    int costFromStart = Infinity;
    BlockPosition previousPosition{};

    // Refine only the winning route, rather than allocating paths on relaxation.
    std::shared_ptr<const RegionSearchTree> tree;
    std::shared_ptr<const NavigationRegion> region;

    int endNodeIndex = -1;
    bool crossesRegionBoundary = false;
};

struct HierarchyQueueEntry {
    int estimatedTotalCost;
    int costFromStart;
    BlockPosition position;

    bool operator>(const HierarchyQueueEntry& other) const {
        if (estimatedTotalCost != other.estimatedTotalCost) {
            return estimatedTotalCost > other.estimatedTotalCost;
        }

        return costFromStart < other.costFromStart; // Prefer progress toward the goal on equal f.
    }
};

void ReconstructHierarchyPath(BlockPosition start, BlockPosition goal,
                              const PositionMap<HierarchyRecord>& records, PathResult& result) {
    MC_PROFILE_ZONE("Reconstruct navigation path");

    std::vector<std::vector<BlockPosition>> segments;

    for (auto position = goal; position != start;) {
        const auto entry = records.find(position);
        if (entry == records.end() || segments.size() >= records.size()) {
            result.status = PathStatus::InvalidState;
            return;
        }

        const auto& record = entry->second;
        std::vector<BlockPosition> segment;

        for (int node = record.endNodeIndex; node >= 0; node = record.tree->parentNodeIndex[node]) {
            segment.push_back(
                PositionFromCellIndex(record.region->nodes[node].cellIndex, record.region->coordinates));
        }

        std::reverse(segment.begin(), segment.end());

        if (record.crossesRegionBoundary) {
            segment.push_back(position);
        }

        segments.push_back(std::move(segment));
        position = record.previousPosition;
    }

    result.cells.push_back(start);

    for (auto entry = segments.rbegin(); entry != segments.rend(); ++entry) {
        result.cells.insert(result.cells.end(), entry->begin() + 1, entry->end());
    }

    result.status = PathStatus::Found;
}
} // namespace Navigation::detail

namespace Navigation {
using namespace detail;

PathResult HierarchicalPathfinder::FindPath(BlockPosition start, BlockPosition goal, size_t nodeBudget,
                                            SearchClock::time_point deadline) {
    PathResult result;
    result.stats.usedHierarchy = true;
    result.worldRevision = m_World.GetNavigationRevision();

    SearchBudget budget{result, nodeBudget, deadline};
    QueryCache query;

    if (!m_Movement.IsValid()) {
        result.status = PathStatus::InvalidAgent;
        return result;
    }

	// Check if the start and goal positions are valid for the agent
    if (!m_Movement.CanStand(m_World, start)) {
        result.status = PathStatus::InvalidStart;
        return result;
    }
    if (!m_Movement.CanStand(m_World, goal)) {
        result.status = PathStatus::InvalidGoal;
        return result;
    }

    PositionMap<HierarchyRecord> records;
    records[start].costFromStart = 0;

    // Min-heap
    std::priority_queue<HierarchyQueueEntry, std::vector<HierarchyQueueEntry>, std::greater<>> open;
    open.push({ManhattanDistance(start, goal), 0, start});

    while (!open.empty()) {
        if (budget.HasTimeLimitExpired()) {
            result.status = PathStatus::BudgetExceeded;
            return result;
        }

        // I hate that pop doesn't return top.
        const auto current = open.top();
        open.pop();

        const auto currentRecord = records.find(current.position);
        if (currentRecord == records.end()) {
            result.status = PathStatus::InvalidState;
            return result;
        }

        if (current.costFromStart != currentRecord->second.costFromStart) {
            continue;
        }

		// Goal reached; reconstruct the path from the records.
        if (current.position == goal) {
            ReconstructHierarchyPath(start, goal, records, result);
            return result;
        }

        if (!budget.TrySpendNode()) {
            result.status = PathStatus::BudgetExceeded;
            return result;
        }

        const auto relax = [&](BlockPosition destination, int cost,
                               std::shared_ptr<const NavigationRegion> region,
                               std::shared_ptr<const RegionSearchTree> tree, int endNodeIndex,
                               bool crossesRegionBoundary = false) {
            const int candidate = current.costFromStart + cost;
            auto& record = records[destination];

            if (candidate >= record.costFromStart) {
                return;
            }

            record = {
                .costFromStart = candidate,
                .previousPosition = current.position,
                .tree = std::move(tree),
                .region = std::move(region),
                .endNodeIndex = endNodeIndex,
                .crossesRegionBoundary = crossesRegionBoundary,
            };

            open.push({candidate + ManhattanDistance(destination, goal), candidate, destination});
        };

        result.stats.abstractExpandedNodes++;

        const auto region = m_Cache.GetOrCreateRegion(ClusterForPosition(current.position), query, budget);

        if (!region || !m_Cache.EnsureEntrances(*region, query, budget)) {
            result.status = PathStatus::BudgetExceeded;
            return result;
        }

        const int sourceNodeIndex =
            region->nodeIndexAtCell[LocalCellIndex(current.position, region->coordinates)];
        const auto tree = m_Cache.GetOrCreateSearchTree(*region, sourceNodeIndex, budget);

        if (!tree) {
            result.status = PathStatus::BudgetExceeded;
            return result;
        }

        if (ClusterForPosition(goal) == region->coordinates) {
            const int index = region->nodeIndexAtCell[LocalCellIndex(goal, region->coordinates)];

            if (index >= 0 && tree->costFromSource[index] != Infinity) {
                relax(goal, tree->costFromSource[index], region, tree, index);
            }
        }

        for (const auto& entrance : region->entrances) {
            if (tree->costFromSource[entrance.sourceNodeIndex] != Infinity) {
                relax(entrance.target, tree->costFromSource[entrance.sourceNodeIndex] + entrance.cost, region,
                      tree, entrance.sourceNodeIndex, true);
            }
        }
    }

    result.status = PathStatus::NoPath;
    return result;
}

} // namespace Navigation
