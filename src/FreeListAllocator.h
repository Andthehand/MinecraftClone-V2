#pragma once

#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>

// Allocates aligned ranges in caller-defined units (GPU octree nodes here).
// First fit splits free ranges; freeing joins adjacent ranges in both directions.
class FreeListAllocator {
public:
    struct Allocation {
        uint32_t offset = 0;
        uint32_t count = 0;
        bool operator==(const Allocation&) const = default;
    };

    explicit FreeListAllocator(uint32_t capacity, uint32_t alignment = 1);

    [[nodiscard]] 
    std::optional<Allocation> Allocate(uint32_t count);
    // Reject invalid/double frees rather than corrupting the free list.
    bool Free(Allocation allocation);
private:
    uint32_t m_Alignment;

	// Key: offset, Value: count for both maps
    std::map<uint32_t, uint32_t> m_FreeRanges;
    std::unordered_map<uint32_t, uint32_t> m_Allocations;
};
