#pragma once
// AY2D.h — umbrella include for the AY2D 2D world subsystem.
//
// Phase 3B ships the animation table + Sprite real impl. Consumers
// should `#include <AY2D.h>` rather than reaching into individual
// `AY<X>.h` files directly. Mirrors the AYPhysics sibling umbrella
// pattern (AYRuntime/AYPhysics/AYPhysics.h + CLAUDE.md "新增模块要求").

#include "AY2DCounters.h"
#include "AYAtlasDesc.h"
#include "AYChunkData.h"
#include "AYChunkRequestHandle.h"
#include "AYOrthographicCamera.h"
#include "AYSprite.h"
#include "AYWorldAabb.h"
#include "AYSpriteDrawCmd.h"
#include "AYTileAnimation.h"
#include "AYTileCoord.h"
#include "AYTileLoadState.h"
#include "AYTileMath.h"
#include "AYTileRect.h"
#include "AYTilemapChunkSource.h"
#include "AYTilemap.h"
#include "AYWorld2D.h"

// P3H.2: read-only value-type snapshot (replaces the
// IWorld2DDebug vtable proposal). No `Entry&` exposure, no
// dangling `resource` pointer — the snapshot holds plain
// handle/layer/sortingKey views plus a relaxed atomic counters
// snapshot. `TilemapBinding` (above) is deprecated.
#include "AYWorld2DSnapshot.h"

// Collision-flag bitmask + operators (declared in design.md §8.1).
// Phase 5 (2026-07-30) ships the full §8.1 type set: `Ray2D`,
// `RaycastHit2D`, `ITileCollisionQuery`. The `TilemapCollisionQueryAdapter`
// concrete adapter is the only in-AY2D implementation; production
// raycast logic lands via cross-module PR (§4.2.1 to AYPhysics).
#include "AYTileCollision.h"
#include "AYTilemapCollisionAdapter.h"
