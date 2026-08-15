#pragma once
// AY2D.h — umbrella include for the AY2D 2D world subsystem.
//
// Phase 3B ships the animation table + Sprite real impl. Consumers
// should `#include <AY2D.h>` rather than reaching into individual
// `AY<X>.h` files directly. Mirrors the AYPhysics sibling umbrella
// pattern (AYPhysics.h + CLAUDE.md "新增模块要求").

#include "AY2D/2DCounters.h"
#include "AY2D/AtlasDesc.h"
#include "AY2D/ChunkData.h"
#include "AY2D/ChunkRequestHandle.h"
#include "AY2D/OrthographicCamera.h"
#include "AY2D/Sprite.h"
#include "AY2D/WorldAabb.h"
#include "AY2D/SpriteDrawCmd.h"
#include "AY2D/TileAnimation.h"
#include "AY2D/TileCoord.h"
#include "AY2D/TileLoadState.h"
#include "AY2D/TileMath.h"
#include "AY2D/TileRect.h"
#include "AY2D/TilemapChunkSource.h"
#include "AY2D/Tilemap.h"
#include "AY2D/World2D.h"

// P3H.2: read-only value-type snapshot (replaces the
// IWorld2DDebug vtable proposal). No `Entry&` exposure, no
// dangling `resource` pointer — the snapshot holds plain
// handle/layer/sortingKey views plus a relaxed atomic counters
// snapshot. `TilemapBinding` (above) is deprecated.
#include "AY2D/World2DSnapshot.h"

// P3H.1: thin wrapper around `AtlasDesc` + a path. Reuses
// `AYTileSamplerUV::tileUV` for per-cell UV. No bgfx handle
// (L-3 lock); the path resolves via the cross-module PR.
#include "AY2D/SpriteSheet.h"

// Collision-flag bitmask + operators (declared in design.md §8.1).
// Phase 5 (2026-07-30) ships the full §8.1 type set: `Ray2D`,
// `RaycastHit2D`, `ITileCollisionQuery`. The `TilemapCollisionQueryAdapter`
// concrete adapter is the only in-AY2D implementation; production
// raycast logic lands via cross-module PR (§4.2.1 to AYPhysics).
#include "AY2D/TileCollision.h"
#include "AY2D/TilemapCollisionAdapter.h"
