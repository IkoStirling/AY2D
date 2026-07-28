#pragma once
// AY2D.h — umbrella include for the AY2D 2D world subsystem.
//
// Phase 0 docs-only state: this header pulls in only the four
// placeholder public headers (design.md §0 / §3). Phase 1+ adds
// ChunkRequestHandle / Ray2D / RaycastHit2D / IAYTilemap / etc.
//
// Consumers should `#include <AY2D.h>` rather than reaching into
// individual `AY<X>.h` files directly. Mirrors the AYPhysics sibling
// umbrella pattern (AYRuntime/AYPhysics/AYPhysics.h + CLAUDE.md
// "新增模块要求").

#include "AYWorld2D.h"
#include "AYTilemap.h"
#include "AYSprite.h"
#include "AYOrthographicCamera.h"

// Collision-flag bitmask + operators (declared in design.md §8.1).
// Phase 0 has no constructor / raycast impl — the enum + operator set
// compile-check the Phase 5 contract surface even before the
// ITileCollisionQuery consumer lands.
#include "AYTileCollision.h"

