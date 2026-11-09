#pragma once

#include "world/Octree.h"
#include "config/Settings.h"

struct Chunk {
    Octree octree{Options::World::CHUNK_OCTREE_DEPTH};
    bool meshDirty = true;
};
