#pragma once
// AY2D.h — umbrella include for the AY2D 2D world subsystem.
//
// Phase 2 ships the first set of CPU-side types. Phase 1+ only had
// placeholder structs. Consumers should `#include <AY2D.h>` rather than
// reaching into individual `AY<X>.h` files directly. Mirrors the
// AYPhysics sibling umbrella pattern (AYRuntime/AYPhysics/AYPhysics.h
// + CLAUDE.md "新增模块要求").

#include "AY2DCounters.h"
#include "AYAtlasDesc.h"
#include "AYChunkData.h"
#include "AYChunkRequestHandle.h"
#include "AYOrthographicCamera.h"
#include "AYSprite.h"
#include "AYTileCoord.h"
#include "AYTileLoadState.h"
#include "AYTilemapChunkSource.h"
#include "AYTilemap.h"
#include "AYWorld2D.h"

// Collision-flag bitmask + operators (declared in design.md §8.1).
// Phase 0 has no constructor / raycast impl — the enum + operator set
// compile-check the Phase 5 contract surface even before the
// ITileCollisionQuery consumer lands.
#include "AYTileCollision.h"
