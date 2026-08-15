// Test_TileCoord.cpp — Phase 1+ stub.
//
// Asserts that the AY2D placeholders compile + instantiate + meet the
// layout invariants locked in design.md §3 + §6.
//
// This test file is purposefully minimal until real TileCoord / World2D /
// Tilemap implementations land in Phase 2. Until then, the assertions
// verify the public-header surface is well-formed (compile pass) and
// that the stand-alone TU pattern (one Test_*.cpp per file, AYAudio
// sibling convention) works under the AY2D / AYTest setup.

#include "AY2D/World2D.h"
#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(TileCoordSuite)

    // Sanity — placeholder header compiles + default-initializes.
    // Real world-coordinate semantics test land in Phase 2 alongside
    // the actual Tilemap implementation (design.md §3 + §6).
    TEST_CASE(World2DDefaultsToZeroEpoch) {
        World2D world;
        CHECK_INT_EQ(static_cast<uint32_t>(world.resourceEpoch), 0u);
    }

    TEST_CASE(World2DResourceEpochSemanticLock) {
        // design.md §3.4: resourceEpoch bumps ONLY when an asset handle
        // resolves a new IResource instance OR addTilemap / removeTilemap
        // / swapTilemap is called. The placeholder default 0 is the
        // "no resource has ever been touched" sentinel — kept here so a
        // future "world is fresh" predicate has a stable baseline.
        World2D fresh;
        CHECK_INT_EQ(static_cast<uint32_t>(fresh.resourceEpoch), 0u);
    }

TEST_SUITE_END
