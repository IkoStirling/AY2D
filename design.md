# AY2D — Design

> **Status**: Phase 0 — docs-only (no code merged yet).  
> **Version**: v0.1 (2026-07-27).  
> **Authority**: This file is the source of truth for `AY2D` module architecture.  
> **Scope of this PR**: design document only. Submodule registration, CMake entries, and source files are intentionally **not** part of this commit.

Related docs:

- [`AYRuntime/docs/first-game-engine-capability-map.md`](../docs/first-game-engine-capability-map.md) — §E W2D-01..07 (the original 2D scope request) and L85 "World tiles/sprites are **not** built on AYUI widgets" hard constraint.
- [`AYRendering-Architecture-Roadmap.md`](../../AYRendering-Architecture-Roadmap.md) — Record/execute split §1, pass matrix §3.2, DrawListBuilder batching key §4, UI parallel §5, public-headers-no-bgfx rule §10.5.
- [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../ENGINE-DETERMINISM-ARCHITECTURE.md) — Present/Sim lane discipline §1.3 / §4 / §6 (forbid Jolt under lockstep).
- [`ENGINE-FOUNDATION-PLAN.md`](../../ENGINE-FOUNDATION-PLAN.md) — three-layer asset model §2.1 / §2.3, RenderSystem pattern §5.6, DrawItem payload pattern §5.7.
- [`AYRuntime/AYRenderer/design.md`](../AYRenderer/design.md) — §13 R5+ 2D migration backlog, §14 testing discipline (sticky-Noop + 3× green baseline).
- [`AYRuntime/AYRenderer/include/AYRenderTypes.h`](../AYRenderer/include/AYRenderTypes.h) — `RenderPassSlot` append-only invariant.
- [`AYRuntime/AYResource/design.md`](../AYResource/design.md) — L1/L2/L3 contract, versioned `.ay*` policy.
- [`AYRuntime/AYPhysics/design.md`](../AYPhysics/design.md) — §4.2 / §6.4 / §17.8 (2D backend TBD; checklist gate).
- [`AYRuntime/AYShader/design.md`](../AYShader/design.md) — §2.1 / §8.5 (ShaderResourcePool + variant tag policy).

---

## 0. Front-matter

| Field | Value |
|---|---|
| Title | `Aliyat Engine — AY2D Module Design` |
| Status | `Phase 0 docs-only. No code merged. Phase 1+ PRs gated on this design sign-off.` |
| Owner | Rendering lane + ECS lane (co-owners; Physics lane reviewer) |
| North-star milestone | Play a tile-scrolled orthographic scene in `AYEditor` with deterministic tile layout, animated tiles, one parallax layer, and a Present-lane character that walks on `ITileCollisionQuery` (flags / cell solid). A full kinematic character + heightmap-backed 2D physics backend is **Phase 5+** (gated on backend pick in §8.3), not part of the Phase 2 demo exit. |
| Out of scope (Phase 0) | UI widgets, Logia scripting of 2D ECS, real Jolt/Box2D physics integration, lockstep tile sim, AYUI integration, mobile platform backend (Noop-validated only) |
| Out of scope (Phase 1+) | Reusing `AYUI` chrome widgets, 3D mesh blending with 2D sprites, runtime tile authoring tools |

**Phase 0 exit criteria** (locked):

1. This `design.md` file exists and contains all chapters §0–§12 + Appendix A.
2. Capability map §E L72-85 text is copied verbatim into Appendix A.
3. Lock decisions L-1..L-17 are each mapped to a specific section of this doc.
4. Risks R-1..R-10 each have a default position and a resolution trigger.
5. The `AY2D/` directory in this repo contains **only** this `design.md` (no submodule registration, no CMake entry, no source files, no CLAUDE.md, no README.md — those land in Phase 1+).
6. Root `CMakeLists.txt` and `.gitmodules` are **unchanged** by this commit.
7. `AYRenderer`, `AYEntity`, `AYResource`, `AYPhysics`, `AYShader` source files are **unchanged** by this commit.

---

## 1. Executive summary

**AY2D** is a **Presentation-lane, float** 2D subsystem of AY Engine. It adds four reusable concepts — `World2D`, `Tilemap`, `Sprite`, `OrthographicCamera` — plus a tile-atlas sampler contract and an ECS extension that flows draws through `AYRenderer`'s existing pipeline.

AY2D does **not**:

- Own any `bgfx::*` type (per [AYRendering-Architecture-Roadmap §10.5](../../AYRendering-Architecture-Roadmap.md)).
- Mix into `RenderScene` / `DrawItem`'s existing 3D fields (it will reuse the `DrawItem::payload` extension pattern described in [ENGINE-FOUNDATION-PLAN §5.7](../../ENGINE-FOUNDATION-PLAN.md) §5.7).
- Depend on `AYUI` (per [capability-map §E L85](../docs/first-game-engine-capability-map.md)).
- Add a 2D physics resolver or pick a 2D backend (per [AYPhysics §4.2](../AYPhysics/design.md) §4.2 "2D backend TBD").
- Reorder or repurpose any existing `RenderPassSlot` enum value (per [AYRenderTypes.h](../AYRenderer/include/AYRenderTypes.h) append-only invariant).

AY2D will, in later phases:

- Introduce `RenderPassSlot::Forward2DOpaque` as an **append-only** new slot (L-6).
- Reuse `Renderer::setMainCamera(view, proj)` for orthographic matrices (the existing API accepts arbitrary `Float4x4`).
- Reuse `ShaderResourcePool` + variant tags for shader management (no new shader file type).
- Reuse `AYResource` L1/L2/L3 layering with two new file types: `.aytilemap` and `.ayatlas`.

**Two-track decision**: Sim-lane 2D world (tile gameplay state, deterministic lockstep) is **optional / deferred** and follows the same dual-path pattern as Physics-A vs Physics-B in [ENGINE-DETERMINISM-ARCHITECTURE.md §4](../../ENGINE-DETERMINISM-ARCHITECTURE.md). Until a product asks for it, AY2D ships Present-only. This matches the philosophy in [capability-map §E W2D-04](../docs/first-game-engine-capability-map.md) marking tile collide/nav as `P2 for this game`.

---

## 2. Module placement & boundary

### 2.1 Placement

| Decision | Lock |
|---|---|
| Submodule path | `D:\Projects\AYRuntime\AY2D\` — new submodule |
| Git URL pattern | `file:///d/Projects/AYRuntime/AY2D` — mirrors `.gitmodules` conventions |
| CMake registration | `add_subdirectory(AYRuntime/AY2D)` appended to root `CMakeLists.txt` **only after** Phase 0 exit + Phase 1 skeleton PR |
| Library type | `STATIC` — matches every existing `AYRuntime/*` entry |
| Build flag | `AY_ENABLE_AY2D` option, default `OFF` for Phase 1, flipped in Phase 2 once TilemapParallaxDemo is green |
| PUBLIC include directory | `${CMAKE_CURRENT_SOURCE_DIR}/include/AY2D` only — no leak of bgfx headers |

### 2.2 Boundary rules

| Rule | Source of truth |
|---|---|
| **Public headers contain zero `<bgfx/bgfx.h>`** | [AYRendering-Architecture-Roadmap §10.5](../../AYRendering-Architecture-Roadmap.md) |
| **Public headers never include `<AYRenderer/src/detail/*>`** | Public/private include discipline in [`AYRenderer/CMakeLists.txt`](../AYRenderer/CMakeLists.txt) |
| **No dependency on `AYUI`** | [Capability-map §E L85](../docs/first-game-engine-capability-map.md) — hard constraint |
| **Public headers carry only opaque handles / paths / math types** | L-3, L-16; mirrors `DrawItem::mesh` / `DrawItem::material` pattern in [AYRenderScene.h](../AYRenderer/include/AYRenderScene.h) |
| **No direct `bgfx::submit` or `ShaderResourcePool::acquire` in ECS or game code** | ECS components hold path / handle, never GPU handle; mirrors [`AYMeshComponent.h`](../AYEntity/include/components/AYMeshComponent.h) |

### 2.3 Allowed dependencies

| Layer | Allowed | Forbidden |
|---|---|---|
| AYFoundation | `AYMath`, `AYIO`, `AYPlatform`, `AYTime`, `AYMemory`, `AYLog`, `AYReflect`, `AYSerializer` | (none) |
| AYRuntime (public headers only) | `AYResource/interface/`, `AYResource/interface/assetsDefs/`, `AYRenderer/include/`, `AYShader/include/` (public), `AYEntity/interface/`, `AYGameLoop/include/` | `AYRenderer/src/detail/*`, `AYUI/`, `AYAnimation/` (tile/sprite anim is AY2D-owned — see R-10), `bgfx::bgfx`, `bgfx::bimg`, anything starting with `<bgfx/` |
| AYPhysics | `IPhysicsBackend2D` interface only (no concrete backend) | `Jolt/Jolt.h`, `Box2D/Box2D.h` |

---

## 3. World2D / Tilemap / Sprite / OrthographicCamera boundary

| Type | Owns | Does NOT own | Suggested file |
|---|---|---|---|
| **`World2D`** | Logical coordinate frame; root node; cell size (`IVector2`); scene graph; asset handles (`TilemapResourceHandle`, `SpriteSheetResourceHandle`); `resourceEpoch` counter (see §3.4) | GPU upload, draw order, sort keys, physics bodies, navigation graph | `include/AY2D/World2D.h` |
| **`Tilemap`** | Grid topology (cols × rows); tile-id array (typed slot — see §5); tile size in world units; per-tile animation table; per-layer transforms; **chunking / streaming policy** (own); collision flags as read-only bitfield | Rendering geometry (delegates to `Renderer` 3D pipeline or to `DrawItem::payload`); physics collider shapes; pathfinding graph | `include/AY2D/Tilemap.h` |
| **`Sprite`** | Single-image or atlas sub-rect draw intent: `worldMatrix`, `sourceRect`, `color`, `flip` bits, `layer`, `sortingKey` | Pixel-perfect vs smooth sampling (must come from material); animation timing | `include/AY2D/Sprite.h` |
| **`OrthographicCamera`** | View-projection (left/right/top/bottom in world units, zoom, rotation); pixel-perfect mode flag; viewport letter-box policy; **layer visibility mask** (`uint32_t layerMask`); orthographic `Float4x4` matrices | Entity iteration; tilemap culling; camera shake logic | `include/AY2D/OrthographicCamera.h` |

### 3.1 Additional locks

- `Tilemap` may extend to **infinite / streaming** maps only via a `TilemapChunkSource` interface (see §6). The default constructor must remain finite-memory and load eagerly.
- `Sprite` MUST NOT carry a `bgfx::TextureHandle`. It carries a `MaterialHandle` (from `AYResource` L2 material) or a path string; the `AYRenderer` material instance is the only thing allowed to reference a `bgfx::*`.
- `OrthographicCamera` MUST serialize/reflect through `AY_PROPERTY` for Editor Inspector (matches the reflect-on-consumer pattern in [ENGINE-SERIALIZER-REFLECT-STATUS.md](../../ENGINE-SERIALIZER-REFLECT-STATUS.md) §5.2).
- All four types live under `namespace ayt::ay2d` (mirroring `ayt::physics`, `ayt::entity`, `ayt::render`).

### 3.3 ECS lane mapping (F-10)

**Audit finding F-10**: `SystemLane` in `AYEntity/design.md §14` is a **design-contract convention only** — there is no enum / tag / annotation / `lane` parameter in code. Systems register solely by integer priority via `World::registerSystem<T>(int32_t priority)`. Lane membership is declared in prose (design doc / PR / code review). Code-level lane metadata is **explicitly deferred to DET-04** (see `ENGINE-DETERMINISM-ARCHITECTURE.md §6`).

AY2D inherits this prose-only convention. Each AY2D system carries a `// Lane: <Present|Sim|Bridge>` header comment, reviewed at PR time:

| AY2D system | Lane | Time source | Register priority | Notes |
|---|---|---|---|---|
| `TilemapRenderSystem` | **Present** | `ayt::time::Clock::gameNow` (scaled, may vary under variable dt) | 510 (after `SkinnedMeshRenderSystem` = 500, before any Sim system) | Walks `World::query<TilemapComponent, Transform>()`; fills `RenderScene2D`. Reads only Present-side tile state. |
| `SpriteRenderSystem` | **Present** | `ayt::time::Clock::gameNow` | 510 | Walks `World::query<SpriteComponent, Transform2D>()`; fills `RenderScene2D`. |
| `TilemapAnimationTickSystem` | **Present** | `ayt::time::Clock::gameNow` (Presentation accumulator) | 460 (before render systems; after `AnimationSystem` = 450 — mirrors 3D pattern in `AYEntity/design.md §15.6` GL-01) | One batched step walks the per-tile animation table; updates frame indices stored on the `Tilemap` L2 object. |
| `TilemapStreamingSystem` (Phase 4) | **Present** | wall clock for IO debounce only; no game-time effect | 430 (before any animation system) | Polls `OrthographicCamera` AABB, requests/recycles chunks via `ITilemapChunkSource`. Sim-lane visibility is the deterministic cache fallback (see §6.3). |
| `TilemapCollisionSystem` (Phase 5+) | **Sim** | `ayt::time::Clock::simNow` (fixed step) | 700+ (after `PhysicsSubSystem`) | Reads `ITileCollisionQuery`; emits collision events to `AYEventSystem`. |
| `TilemapPathfindingSystem` (Phase 7+) | **Sim** | `ayt::time::Clock::simNow` | 700+ | Reads `IPathfinder`; emits path results via EventBus or component. |
| `OrthographicCameraUpdateSystem` | **Present** | `ayt::time::Clock::gameNow` | 405 (early — before render) | Updates `OrthographicCamera::view/proj` matrices; push to `Renderer::setMainCamera`. |

The registration helper in Phase 1+ is `register2DSystem<T>(int32_t priority)` in `AY2D/include/AY2D/AYEntityIntegration.h`. It mirrors the existing `registerAnimationSystem()` / `registerSkinnedMeshRenderSystem()` pattern (`AYEntity/include/AYEntityModule.h`). It MUST be idempotent (re-check `World::hasSystemNamed` before adding — same pattern as `AYEntityModule.cpp:28-74`).

**No lane parameter is added to `World::registerSystem` in Phase 0** — that change belongs to DET-04.

### 3.4 World2D's `resourceEpoch` (F-14)

The `version counter` mentioned in the World2D boundary table is concretely named **`resourceEpoch`** (`uint64_t`). It is **bumped only** when one of the following happens:

1. A `TilemapResourceHandle` or `SpriteSheetResourceHandle` resolves a new `IResource` instance from the AYResource cache (e.g. hot-reload re-instance path, see §6.2.1).
2. The `World2D::addTilemap` / `removeTilemap` / `swapTilemap` API is called.

It is **not** bumped on:

- Per-frame draw submission (`RenderSystem2D::buildScene`).
- ECS component changes that only touch `Transform` / `Transform2D`.
- Animation frame advance.

This makes `resourceEpoch` a cheap "did anything resource-shaped change since last frame?" predicate for systems that want to skip work on identical frames (e.g. `TilemapStreamingSystem`'s hot path).

### 3.2 Dependency arrow (canonical answer to "where does AY2D live?")

```
ECS (AYEntity)            Renderer (AYRenderer)            bgfx
RenderSystem2D  ----->    Draw2DPass (Forward2DOpaque slot)
                  <-----  RenderScene2D + DrawItem.payload
                  <-----  ShaderResource (variant: tilemap / sprite / sprite-9tap)
                                  |
                                  v
                          BGFXAdapter -----------------> bgfx
```

This mirrors the arrow diagram in [AYRendering-Architecture-Roadmap §1](../../AYRendering-Architecture-Roadmap.md).

---

## 4. Dependency direction matrix

Strict one-way graph. Forbidden cells must compile-fail in the public-header surface test.

| From \ To | AYResource | AYRenderer (public) | AYEntity | AYPhysics | AYUI | AYScript | AYShader (public) | AYAnimation | bgfx |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **AY2D public** | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | ❌ | ❌ |
| **AY2D internal `detail/`** | ✅ | ✅ (only `DrawItem`, `MaterialHandle`, `RenderPassSlot`) | ❌ | ❌ | ❌ | ❌ | ✅ (via `ShaderResource` interface) | ❌ | ❌ |
| **AYResource ↔ AY2D** | n/a | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **AYRenderer ↔ AY2D** | ❌ | n/a | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| **AYEntity ↔ AY2D** | ❌ | ❌ | n/a | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **AYPhysics ↔ AY2D** | ❌ | ❌ | ❌ | n/a | ❌ | ❌ | ❌ | ❌ | ❌ |

### 4.1 Locks

- **L-15**: AY2D does **not** push components into `AYEntity` directly; it ships **components** that ECS users opt in to via a `register2DSystem()` helper analogous to `registerAnimationSystem()` in [`AYEntityModule.h`](../AYEntity/include/AYEntityModule.h).
- **L-5**: AY2D does **not** modify `AYRenderer` public headers beyond the documented `DrawItem::payload` append (see §5.4 + §7 of [ENGINE-FOUNDATION-PLAN](../../ENGINE-FOUNDATION-PLAN.md)).
- **L-4**: AY2D does **not** link `AYUI`. Editor glue lives in `AYEditor`, not `AY2D`.
- **L-16**: ECS components hold only path / opaque handle / world matrix. GPU handles never leave `AYRenderer`.

### 4.2 Forbidden cross-module writes

| File | Must not change in Phase 0 PR |
|---|---|
| `d:\Projects\CMakeLists.txt` | (untouched) |
| `d:\Projects\.gitmodules` | (untouched) |
| `d:\Projects\AYRuntime\AYRenderer/include/AYRenderTypes.h` | (untouched; slot append is Phase 1+) |
| `d:\Projects\AYRuntime\AYRenderer/include/AYRenderScene.h` | (untouched; payload append is Phase 2+) |
| `d:\Projects\AYRuntime\AYRenderer/src/detail/*` | (untouched) |
| `d:\Projects\AYRuntime\AYEntity/include/components/*` | (untouched; new components in Phase 2+) |
| `d:\Projects\AYRuntime\AYEntity/CMakeLists.txt` | (untouched; static `set(SOURCES ...)` requires manual edit in Phase 2+) |
| `d:\Projects\AYRuntime\AYResource/interface/assetsDefs/*` | (untouched; `IAYTilemap.h` / `IAYTileset.h` in Phase 2+) |

#### 4.2.1 Cross-module PR ownership (F-6)

When AY2D later needs to modify a file owned by another module (e.g. Phase 2 must add `RenderPassSlot::Forward2DOpaque` to `AYRenderer/include/AYRenderTypes.h`, `DrawItem::payload` to `AYRenderer/include/AYRenderScene.h`, `AYTileMapComponent` to `AYEntity/include/components/`, `IAYTilemap.h` to `AYResource/interface/assetsDefs/`), the rules are:

| Cross-module change | Owned by | AY2D author role | Merge gate |
|---|---|---|---|
| `RenderPassSlot` append, `DrawItem` payload, RenderScene 2D shape | `AYRenderer` maintainer | Submit cross-module PR, **do not self-merge** | Reviewed + merged by `AYRenderer` maintainer. The AY2D PR description must reference the AY2D design §1 / §3 / §7.1 lock rationale. |
| ECS component / system / `register2DSystem()` helper, `World::query<TileMapComponent, Transform>` patterns | `AYEntity` maintainer | Submit cross-module PR, do not self-merge | Reviewed + merged by `AYEntity` maintainer. The PR must not break existing `set(SOURCES ...)` static list — new `.cpp` files are appended. |
| `IAYTilemap.h` / `IAYTileset.h` / `.aytilemap`+`.ayatlas` Loader/Converter/`assetsImpl` four-piece | `AYResource` maintainer | Submit cross-module PR, do not self-merge | Reviewed + merged by `AYResource` maintainer. The PR must respect L1/L2/L3 layering (§9.3) and the `.aymesh` extension-chunk precedent (§9.2). |
| New shader variant tags (`tilemap.phoskia`, `tilemap_9tap.phoskia`) | `AYShader` maintainer | Submit cross-module PR, do not self-merge | Reviewed + merged by `AYShader` maintainer. No new shader file type. |
| 2D collision query interface (`ITileCollisionQuery` consumer side) | `AYPhysics` maintainer | Submit cross-module PR, do not self-merge | Reviewed + merged by `AYPhysics` maintainer. AY2D never owns a physics backend. |

**The AY2D submodule's own CI is forbidden from merging into other modules' branches.** Cross-module merges go through the existing per-module maintainer workflow. This is what "AY2D does not modify other modules' source in Phase 0" means in practice for Phase 1+.

---

## 5. Tile atlas, sampler, texel-center & filter semantics

> This chapter is where several internet sources conflate unrelated optimizations. The text below uses precise terminology and **does not** claim equivalence between unequal paths.

### 5.1 Atlas layout & coordinates (locked)

- Atlas is a single texture (`.ayatlas`, see §9); tile size is **constant** per atlas (e.g. `32×32`); sub-rect uses **integer** `texelIndex` not floats.
- Atlas size in texels: `(atlasWidthTexels, atlasHeightTexels)` stored as `uint32_t`. Atlas size in bytes derived from `TextureFormat`.
- World coordinate frame is `IVector2` cells (per [AYMath/aymath/MathTypes.h](../../AYFoundation/AYMath/include/aymath/MathTypes.h)); this is integer-grid, **not** fixed-point. Sim lane can use it without determinism concerns.
- **Pixel-center convention (L-7)**: the texel center is at the integer coordinate. A tile at `(0, 0)` in tile space samples atlas `[0.5, tileSize - 0.5]` in texel units (or, equivalently, `[tileSize/(2*atlasW), tileSize/(2*atlasW) ± ...]` in UV space). This is the standard "half-texel offset" rule and is **only** relevant for nearest filtering at exact integer world positions to avoid edge bleed into the neighboring tile.
- The half-texel offset does **not** by itself change the number of texture fetches. It only changes the sample position.
- **Half-texel offset does not** make a 3×3 sampling equal to a 2×2 sampling. See §5.4.

### 5.2 Gutter / extrusion (locked)

- Atlas format supports per-tile gutter width (`AtlasDesc::gutter`, default `1` texel). When `gutter > 0`, the sampler fetches the neighboring texel color for edge taps to eliminate bilinear bleed across tile boundaries.
- Gutter requires **bilinear** filtering to be meaningful. With `Nearest`, gutter is a wasted region.
- Gutter size is an atlas metadata field, **not** a per-tile field. Validation rule: `gutter < min(tileWidth, tileHeight) / 2`.

### 5.3 Filter modes — true behavior table

| Filter | Behavior at integer world position | Behavior at non-integer (half-pixel) | Uses gutter? |
|---|---|---|---|
| `Nearest` | Samples **one** texel (integer floor). Edge bleed if world position == atlas tile boundary. | Same as integer (no interpolation). | **No** — recommend gutter = 0 to save memory. |
| `Bilinear` | Samples 2×2 neighbors, weighted. Edge bleed if `gutter == 0`. | Smooth interpolation across atlas. | **Yes** — `gutter ≥ 1` eliminates edge bleed. |
| `Bilinear + custom 9-tap` (see §5.4) | 9 explicit taps via custom shader. Rotational smoothing. | Sharper than bilinear under rotation. | Optional — see §5.4. |

**Truth about "pixel art"** (locked): the common claim "nearest + half-texel = perfect pixel art" is true **only** when:

1. World positions always land on integer pixel centers, AND
2. Camera zoom is an integer, AND
3. Atlas has `gutter == 0`, AND
4. Viewport scale is an integer (no DPI scaling).

If any of those four breaks, pixel art will show artifacts (pixel crawl, aliasing, sparkle). AY2D will document this in `unittest/Test_TilemapSampler.cpp` with explicit visual golden-pixel tests at each invariant.

### 5.4 4-tap vs 9-tap — what they actually are (locked, no equivalence invented)

| Operation | Tap count (effective) | Where it lives | Use case |
|---|---|---|---|
| **Bilinear sample (1 texel)** | 4 texel fetches per output pixel (2 in U × 2 in V) | GPU texture unit (hardware) | Smooth scaling, rotation, sub-pixel camera. |
| **Bilinear + mip LOD bias** | 4 texel fetches + 1 mip-LOD bias uniform | Shader uniform `sampler2D` with `texture2dLod` | Soft tile atlases with mips. |
| **9-tap weighted filter** | 9 texel fetches per output pixel (3×3 grid), weights applied in shader | Shader (custom); **NOT** hardware bilinear | Rotational smoothing, "fake trilinear", custom edge-aware blur. |
| **Trilinear mip** | 8 texel fetches (4 per mip × 2 mips) | GPU | Mipmaps for 3D textures; not normally meaningful for tile atlases (use only if you pre-generate mips for scaled-camera use). |

**L-10 lock**: The default tile sampler is **hardware bilinear = 4 effective taps**. A 9-tap variant is **opt-in** and is **a custom shader**, not equivalent to bilinear under rotation or non-uniform scale. AY2D **must not** claim that 9-tap is "just better bilinear" or "replaces 4-tap with the same result".

The 9-tap shader variant is added to `AYShader` via a **variant tag**, not as a new file type (matches [.aymat variant policy](../AYResource/design.md) §4.2 row for `.aymat`).

### 5.5 Sampler contract

```cpp
// include/AY2D/AYTileSampler.h — Phase 0 interface shape only (not a real header yet)
namespace ayt::ay2d {

enum class TileFilter : uint8_t {
    Nearest = 0,
    Bilinear = 1,
    Bilinear9Tap = 2,  // opt-in, requires shader variant tag "tilemap_9tap"
};

// F-15: per-axis wrap. Tilemap atlas is normally Clamp (no edge bleed);
// scroll / parallax backgrounds need Repeat. Mirror is reserved for
// shader-side effects (e.g. animated water). Border is rejected — the
// gutter already covers edge bleed for Bilinear.
enum class TileWrap : uint8_t {
    Clamp  = 0,
    Repeat = 1,
    Mirror = 2,
};

struct AtlasDesc {
    uint32_t    atlasWidthTexels  = 0;
    uint32_t    atlasHeightTexels = 0;
    uint32_t    tileWidthTexels   = 0;
    uint32_t    tileHeightTexels  = 0;
    uint32_t    tilesPerRow       = 0;
    uint32_t    tilesPerColumn    = 0;
    uint32_t    gutter            = 1;   // L-8 default
    TileFilter  filter            = TileFilter::Bilinear;
    TileWrap    wrapU             = TileWrap::Clamp;  // F-15 default
    TileWrap    wrapV             = TileWrap::Clamp;
};

} // namespace ayt::ay2d
```

The shader pipeline uses this filter to select the correct variant at material load time. Materials with `Bilinear9Tap` will require the `tilemap_9tap` shader variant to be compiled into the asset bundle; offline toolchain refuses to bake such materials if the variant is missing.

---

## 6. World limits, chunking & streaming

### 6.1 Two map sizes

**Tile-id storage width is locked to a single mode per resource** (P0 / F-1). Mixing widths in the same data path is forbidden.

| Tile-id mode | Per-tile bytes | Max distinct tiles | Memory for 16 M tiles | When to use |
|---|---|---|---|---|
| **`TileIdPackMode::Narrow16`** (default) | 2 | 65 535 | 32 MB | Default; 65 k distinct tile types covers any sane pixel-art or modern 2D title. |
| **`TileIdPackMode::Wide32`** | 4 | 4 294 967 295 | 64 MB | Used only when the title legitimately exceeds 65 k tile types (essentially never). Converter must justify the wide mode in a code-review comment. |

| Map mode | When | Cost | Constraint |
|---|---|---|---|
| **Finite** | `cols × rows` known at load; full memory-resident | O(cols × rows) bytes (`Narrow16` = 2 B/tile; `Wide32` = 4 B/tile) | `cols * rows <= kMaxFiniteTilemapTiles = 16 * 1024 * 1024` (16 M tiles ≈ 32 MB narrow / 64 MB wide) |
| **Streaming** | `TilemapChunkSource` interface; chunks `16×16` tiles default; load on demand | O(loaded chunks × tile count) bytes; bounded by `TilemapBudget` | Chunk IO on Present lane only |

The chosen `TileIdPackMode` is part of `.aytilemap` header (see §9.1) — `mode` field — and is enforced by the loader. Loaders refuse a wide-mode file when the runtime is configured narrow; runtime wide-mode decision is also a code-review gate.

### 6.2 `TilemapChunkSource` interface (Phase 0 shape only)

```cpp
// include/AY2D/AYTilemapChunkSource.h — Phase 0 interface shape only
namespace ayt::ay2d {

// Forward-declared opaque async token. Phase 1+ provides the concrete
// type (likely a 32-bit request id with a "generation" bit for ABA safety).
struct ChunkRequestHandle {
    uint32_t id = 0;
    bool     isValid() const noexcept { return id != 0; }
};

struct ChunkCoord {
    int32_t x = 0;
    int32_t y = 0;
};

enum class TileIdPackMode : uint8_t {
    Narrow16 = 0,   // default; see §6.1
    Wide32   = 1,
};

struct ChunkData {
    ChunkCoord            coord;
    TileIdPackMode        mode = TileIdPackMode::Narrow16;
    // tileIds.size() == tileW * tileH; the storage width follows `mode`.
    //   Narrow16 → std::vector<uint16_t> is the natural layout.
    //   Wide32   → std::vector<uint32_t>.
    // AY2D never sees a mixed-width buffer.
    std::vector<uint16_t> tileIds16;
    std::vector<uint32_t> tileIds32;
    uint64_t              versionStamp = 0;
};

enum class EvictionPolicy : uint8_t {
    LRU         = 0,   // default; mirrors AYResourceCache
    Distance    = 1,   // chunk farthest from active camera evicted first
    TimeWindow  = 2,   // chunks untouched for N seconds evicted first
};

struct TilemapBudget {
    uint32_t       maxChunksLoaded    = 1024;     // soft cap
    uint32_t       maxChunksResident  = 2048;     // GPU residency
    uint32_t       maxIoBytesPerSec   = 64 * 1024 * 1024;  // 64 MB/s
    EvictionPolicy eviction           = EvictionPolicy::LRU;
};

class ITilemapChunkSource {
public:
    virtual ~ITilemapChunkSource() = default;
    // Async chunk request. Returns a future the caller may poll.
    // Cancellation is supported via the returned token.
    virtual ChunkRequestHandle requestChunk(ChunkCoord coord) noexcept = 0;
    virtual bool               tryGetChunk(ChunkCoord coord, ChunkData& out) const noexcept = 0;
    virtual void               cancelChunk(ChunkRequestHandle handle) noexcept = 0;
    // Returns true if the chunk is currently resident in the cache (present).
    virtual bool               isResident(ChunkCoord coord) const noexcept = 0;
};

} // namespace ayt::ay2d
```

> **Hot reload** (P1 / F-11): `ITilemapChunkSource` is itself reload-aware. A reload event on `.aytilemap` invalidates the strong cache entry (mirrors `AYResource/design.md §6.5`); outstanding `ChunkData` are not held by the cache beyond their delivery. The `.aytilemap` reload path uses `AYHotReloadWatcher`'s whole-second mtime (see §6.2.1).

#### 6.2.1 Hot-reload policy (F-11)

AY2D inherits the existing engine hot-reload policy rather than inventing a new one:

- **Whole-second mtime polling** via `AYHotReloadWatcher` (default 1.0 s interval; see `AYResource/include/AYHotReloadWatcher.h:57`).
- **No version stamps** are bumped at runtime — re-instance is purely mtime-driven (per `AYResource/design.md §6.5` quoted verbatim in the audit).
- **GUID** (per `IAYConverter.h:16` `ayt::math::FGuid guid`) is **content identity** for cache dedup, **not** a reload trigger. No GUIDatabase exists in the engine (audit finding).
- **Handle LRU** via `AYResourceCache::registerHandle` — the strong-cache entry stays until the last `AYResourceHandle<Tilemap>` drops; the watcher path removes the strong entry on mtime change, so the next access creates a brand-new `IAYTilemap` instance.
- The chunk source subscribes to `ResourceManager::setOnHotReload`; on `.aytilemap` change, the source re-resolves its root resource and re-issues pending requests against the new content.

### 6.3 Determinism caveat (L-11, locked)

Chunk loads are **not** deterministic across machines if IO time varies; therefore chunk loads **must not** affect simulation outcomes in lockstep. This matches [ENGINE-DETERMINISM-ARCHITECTURE §1.2](../../ENGINE-DETERMINISM-ARCHITECTURE.md) ("Presentation World runs every frame, variable or scaled deltaTime").

Concrete lock:

- Chunk IO happens in **Present lane only**.
- Sim-lane systems read tiles through a deterministic cache that returns `defaultTileId` for not-yet-loaded chunks.
- The default tile id is **baked into the resource metadata** (not a runtime constant), so different tilemaps can have different defaults (e.g. "void" → 0 vs "ocean" → 7).

---

## 7. Batching, animation, flip, layers

### 7.1 Batching

- AY2D produces `DrawItem`s that flow through `DrawListBuilder` per [AYRendering-Architecture-Roadmap §4](../../AYRendering-Architecture-Roadmap.md).
- The 2D-specific batch key adds `passMask = Forward2DOpaque`. Atlas (`MaterialHandle`) becomes part of the batch key — different atlas = different batch, since bilinear bleed makes per-atlas sampling stateful.
- `DrawItem::payload` (the Phase 2+ append per L-5) carries:
  - `sourceRect` (4 floats in UV space)
  - `tint` (4 floats RGBA)
  - `flip` (2 bits)
  - `layer` (uint8_t)
  - `sortingKey` (int32_t)

### 7.2 Animated tiles

- Per-tile animation table: `(tileId → [frameTileId, durationMs]*)` lives on `Tilemap`. Frame change is **batched** per system tick (one update step walks the table).
- Animation timing source: `AYTime` (`ayt::time::Clock::gameNow` for Present lane; `ayt::time::Clock::simNow` if/when Sim lane is added). Frame timing is integer-millisecond accumulated remainder to avoid float drift across long play sessions.

### 7.3 Sprite flips

- 2-bit flag on `Sprite::flip` (horizontal, vertical).
- Encoded into the vertex stream via instance data buffer (no extra vertex buffer).
- Flip does **not** break instancing — it is a per-instance bit, not a state change.

### 7.4 Layers and sort keys

- Integer `layer` (0..31) on sprite/tilemap.
- Final sort key: `(layer << 24) | (sortingKey & 0x00FFFFFF)`.
- Editor allows layer rename + visibility mask via `OrthographicCamera::layerMask`.
- **`Draw2DPass::execute` MUST use `std::stable_sort`** (not `std::sort`) because equal sort keys must preserve authoring / draw-order intent. `std::sort` is unstable in every C++ standard; this is not a C++17-specific issue (audit F-2).

---

## 8. Collision & navigation interface (defer backend)

### 8.1 `ITileCollisionQuery` interface (Phase 0 shape only)

```cpp
// include/AY2D/AYTileCollision.h — Phase 0 interface shape only
namespace ayt::ay2d {

// 2D ray used for tile queries. Phase 1+ may reuse ayt::physics::Ray2D
// (when the 2D physics backend ships); today it's defined locally to keep
// AY2D's public header self-contained.
struct Ray2D {
    ayt::math::FVector2 origin    { 0.0f, 0.0f };
    ayt::math::FVector2 direction { 1.0f, 0.0f };  // unit length
    float               tMin      = 0.0f;
};

struct RaycastHit2D {
    IVector2           cell      { 0, 0 };   // tile cell that was hit
    CollisionFlags     flags     = CollisionFlags::None;
    float              distance  = 0.0f;    // along ray.direction
    ayt::math::FVector2 point    { 0.0f, 0.0f };  // world-space hit point
    ayt::math::FVector2 normal   { 0.0f, 0.0f };
};

// Bit flags. Empty (1<<6) is the explicit "this tile has no collision at all";
// None (0) is the default-constructed zero, which means "unset / unknown" —
// it MUST NOT be used to mean "empty". The loader normalizes unknown bits to
// Empty before exposing to the query API.
enum class CollisionFlags : uint32_t {
    None      = 0,
    Solid     = 1u << 0,
    OneWay    = 1u << 1,
    Slope_L   = 1u << 2,
    Slope_R   = 1u << 3,
    Hazard    = 1u << 4,
    Ladder    = 1u << 5,
    Empty     = 1u << 6,
    // Reserved bits 7..31 for future tile meta (sound, material, etc.)
};

// Bitwise operators are required because CollisionFlags is used as a
// bitmask in production paths (e.g. `flags & Solid`).
inline constexpr CollisionFlags operator|(CollisionFlags a, CollisionFlags b) noexcept {
    return static_cast<CollisionFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr CollisionFlags operator&(CollisionFlags a, CollisionFlags b) noexcept {
    return static_cast<CollisionFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline constexpr CollisionFlags operator^(CollisionFlags a, CollisionFlags b) noexcept {
    return static_cast<CollisionFlags>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
}
inline constexpr CollisionFlags operator~(CollisionFlags a) noexcept {
    return static_cast<CollisionFlags>(~static_cast<uint32_t>(a));
}
inline CollisionFlags& operator|=(CollisionFlags& a, CollisionFlags b) noexcept { a = a | b; return a; }
inline CollisionFlags& operator&=(CollisionFlags& a, CollisionFlags b) noexcept { a = a & b; return a; }

class ITileCollisionQuery {
public:
    virtual ~ITileCollisionQuery() = default;

    // Returns the bitmask at `cell`. Loader guarantees Empty is set when no
    // flag applies. Never returns None unless the tile is genuinely unset.
    virtual CollisionFlags flagsAt(IVector2 cell) const noexcept = 0;

    // Raycast through the tile grid. Returns true on first hit. Caller may
    // request a layer mask (Solid | OneWay | Hazard | …) to filter.
    virtual bool raycast(Ray2D ray,
                         float maxDistance,
                         CollisionFlags layerMask,
                         RaycastHit2D& out) const noexcept = 0;

    // Default impl: `flagsAt(cell) & (Solid | OneWay | Hazard) != CollisionFlags::Empty`.
    // Override only when the product needs finer granularity.
    virtual bool isBlocked(IVector2 cell) const noexcept = 0;
};

} // namespace ayt::ay2d
```

### 8.2 No collision resolver in AY2D (locked)

- AY2D ships **no** collision resolver. It exposes `ITileCollisionQuery` returning a tile id + flags at an `IVector2` cell.
- Product code (or a future AYPhysics 2D backend) implements the resolver.
- Navigation is **out of scope** for Phase 0 docs; explicitly deferred to Phase 5+.

### 8.3 2D backend candidates (L-12, all equal priority)

| Backend | Status | Why candidate |
|---|---|---|
| Jolt-2D | **Unverified** at time of writing — Jolt upstream does not currently ship a first-party 2D backend; do not assert availability | If/when Jolt upstream adds 2D, reuse `IPhysicsBackend2D` |
| Box2D | Mature, MIT-like license, widely used in 2D engines | Standard choice |
| Chipmunk2D | Mature, MIT license | Alternative to Box2D with different feature set |
| Custom deterministic subset | R3+ per [ENGINE-DETERMINISM-ARCHITECTURE §6](../../ENGINE-DETERMINISM-ARCHITECTURE.md); **gated on DET-01 Fixed32 types** | Required for Sim-lane lockstep |

**F-13 — DET-01 trace**: at the time of this design, `ayt::Fixed32` (the deterministic arithmetic type that gates the "custom det subset" backend) is **not yet shipped**. The "custom det subset" row above must not be started until DET-01 lands in `AYFoundation`. Phase 5 sign-off requires a one-line trace `DET-01 status: shipped in <commit> on <date>` (or "not yet shipped — defer").

This design **does not** pick a backend. Phase 5 sign-off requires a separate decision document, not a default selection in this design.

---

## 9. Versioned assets & determinism

### 9.1 New L1 file types (L-13)

| Format | Magic | Version | Purpose | Status |
|---|---|---|---|---|
| `.aytilemap` | `'AYTM'` | v1 | Tilemap data (tile id array, layer table, animation table, collision flags, chunk metadata, `defaultTileId`) | Phase 2+ |
| `.ayatlas` | `'AYAT'` | v1 | Texture + atlas metadata (image path, tile size, gutter, filter mode, padding) | Phase 2+ |

### 9.2 Versioning policy

Loaders accept N-1; converters emit current N. Disk format changes go through **Extension four-cc chunks** before spawning a new file type. This matches the policy in [AYResource/design.md §4.1](../AYResource/design.md) and the [.aymesh extension chunk precedent](../AYResource/design.md) §4.3.

### 9.3 L1/L2/L3 layering

| Layer | Lives in | Contains |
|---|---|---|
| L1 (disk) | `.aytilemap` / `.ayatlas` on disk | header + chunks + payload |
| L2 (CPU runtime) | `AYResource` L2 cache (handed out via `IAYTilemap` / `IAYTileset` in `AYResource/interface/assetsDefs/`) | decoded structs, **no GPU handle** |
| L3 (GPU runtime) | `AYRenderer` `RenderResourceManager` | uploaded atlas texture + tile-batch vertex/index buffers |

This is a strict subset of the L1/L2/L3 layering in [ENGINE-FOUNDATION-PLAN §2.1](../../ENGINE-FOUNDATION-PLAN.md).

### 9.4 Determinism boundary

| State | Lane | Storage type |
|---|---|---|
| Tile rendering (UV, vertex, sort key) | Present | `float` (acceptable; render is visual-only) |
| Sprite animation (frame index, accumulator) | Present | `uint32_t` ms remainder accumulator + `uint32_t` frame index (integer-ms; see §7.2 — no float drift) |
| Camera transform | Present | `float` |
| Tile-level gameplay state (broken / damp / on-fire) | **Optional Sim** (Phase 5+) | `Fixed32` (gated on DET-01) |

Until DET-01 ships, AY2D **must not** put gameplay-affecting state into float storage without a `SystemLane::Sim` annotation (mirrors [AYEntity/design.md §14](../AYEntity/design.md)).

---

## 10. Testing & performance budget

### 10.1 Performance budget (locked numbers, no "fast enough" handwaving)

**Reference hardware (F-7)** — all "ms" / "MB" numbers below are measured on this SKU and reported with the SKU in the test log. Budget is loose enough to allow the previous-gen integrated-GPU tier to stay within 2× the number; tighter-tier (dGPU) is expected to beat it.

| Component | Reference SKU | Notes |
|---|---|---|
| CPU | Intel Core i7-12700 (desktop, 12C/20T) | Comparable AMD: Ryzen 7 5800X; comparable mobile: i7-12700H. |
| GPU | NVIDIA RTX 3060 (desktop, 12 GB) | Comparable mobile: RTX 3060 Laptop; comparable AMD: RX 6600. |
| RAM | 32 GB DDR4-3200 |  |
| OS | Windows 11 22H2 |  |
| Driver | stock (no DXVK / no VK_LAYER_* hacks) | AY2D budgets do NOT depend on specific DXVK paths. |

| Metric | Budget | How measured |
|---|---|---|
| Tilemap 64×64 visible, 1 layer, animated | ≤ 0.4 ms CPU + GPU on reference desktop | `ayt::time::Clock` scope around `RenderSystem2D::buildScene` |
| 10 000 sprites (single atlas, no skin) | ≤ 1.0 ms total submission + draw | One frame capture in `Test_Render2D_10kSprites.cpp` |
| Streaming chunk load latency | ≤ 16 ms p99 on warm cache | Telemetry counter `ay2d_chunk_io_us` (see §10.1.1) |
| Atlas memory ceiling | ≤ 64 MB default; user-overridable | Runtime introspection `Tilemap::atlasMemoryBytes()` |
| First-frame determinism | All sprites + tilemap layers populated deterministically, no async gap visible | Visual golden-pixel test in `Test_FirstFrameDeterminism.cpp` |

The 32×32 finite-tilemap MVP from Phase 2 must hit these numbers on the reference SKU before any demo can ship. Performance regressions are caught by `Test_Budget_Render2D.cpp` (gated by `AY_PERF_BUDGETS=1`) which runs nightly against the reference SKU.

#### 10.1.1 Profiling counter naming (F-8)

**Audit finding F-8**: the engine has **no global profiler convention**. `AYProfiler` does not exist; there are no `AY_PROFILE_*` / `ScopedTimer` / `ProfileScope` macros; no `AYTelemetry` / `AYCounter` / `AYMetrics` types in `AYFoundation`. Profiling surfaces are **per-module ad-hoc POD structs** (`AYGameLoop/FrameStats.h`, `AYRenderer/AYRenderTypes.h:47-54 RenderFrameStats`, `AYRenderer/AYShadowDiagnostics.h:23-41 ShadowFrameStats`) plus per-instance atomic counters in `AYPhysicsManager.h:77-79` and `EventBus.h:141-148`.

AY2D does **not** introduce a global profiler. Instead it adopts the **closest documented convention** — the snake-case `<module>_<metric>_<unit>` shape used in `AY2D/design.md` (this file) and in `AYPhysics/design.md:384` (`queueHighWater`, `queueRejectCount`, `syncQueryWaitUs`):

| Counter | Type | Unit | What it counts |
|---|---|---|---|
| `ay2d_chunk_io_us` | `uint64_t` | microseconds | Wall time per chunk IO (`ITilemapChunkSource::requestChunk` → `tryGetChunk` ready) |
| `ay2d_chunk_io_bytes` | `uint64_t` | bytes | Decoded bytes per chunk delivered to the cache |
| `ay2d_chunk_resident_count` | `uint32_t` | count | Number of chunks currently resident |
| `ay2d_atlas_bytes` | `uint64_t` | bytes | Sum of all `.ayatlas` textures currently resident in L3 |
| `ay2d_draw2d_items` | `uint32_t` | count | Per-frame `Draw2DItem` count submitted by `RenderSystem2D` |
| `ay2d_draw2d_pass_us` | `uint64_t` | microseconds | Wall time of `Draw2DPass::execute` |

All counters live on the owning `World2D` / `Tilemap` / `Draw2DPass` instance as `std::atomic<uint64_t>` / `std::atomic<uint32_t>` fields (mirrors `AYPhysics/include/AYPhysicsManager.h:77-79` `_queueHighWater` / `_queueRejectCount`), **not** as TU-static globals. The naming and storage mirror `AYVoxel/design.md:1641-1653` `g_loadedChunkCount` / `g_meshBytes` / `g_drainRejectCount` *only as a precedent*, not as a dependency — AY2D does not link AYVoxel.

> **Naming inconsistency caveat (F-8 transparency)**: the `ay2d_*` prefix uses snake_case while `AYGameLoop/FrameStats.h` and `AYRenderer/AYRenderTypes.h:47-54` use CamelCase POD struct fields. This is a **module-local** style, not a global convention. A future `AYProfiler` module may unify these; until then AY2D's choice is documented here and not propagated as "the engine convention".

### 10.2 Test categories

| Category | Example file | Notes |
|---|---|---|
| Unit | `Test_TilemapSampler.cpp` (UV math, gutter bounds, tile-id packing); `Test_TileChunkSource.cpp` (deterministic cache fallback); `Test_OrthographicCamera.cpp` (projection matrix); `Test_CollisionFlags.cpp` (bitmask ops + `Empty` / `None` semantics); `Test_TileIdPackMode.cpp` (`Narrow16` / `Wide32` round-trip) | AYTest framework; each `Test_*.cpp` is independent TU |
| Hot reload | `Test_HotReload_Tilemap.cpp` (F-17) — touch `.aytilemap` mtime; verify `IAYTilemap` handle re-points to a new `IResource` instance on next access; same path for `.ayatlas`. Uses `AYHotReloadWatcher`'s whole-second mtime polling. | Mirrors the `AYResource/design.md §6.5` reload contract; the AY2D test is a 2D-flavored copy of the existing resource reload test pattern. |
| Integration | `TilemapParallaxDemo` — finite map + animated tiles + parallax + 1 sprite + 1 orthographic camera | Mirrors `AYEngineIntegration_Demo` shape |
| Visual | `Test_PixelCenter_GoldenPixel.cpp` — render a known tilemap, compare to golden PNG at nearest + integer position + half-pixel position | Requires Noop backend; Noop must honor `setViewportRect` for stable capture |
| Performance | `Test_Budget_Render2D.cpp` — gated by `AY_PERF_BUDGETS=1` | Run on reference desktop only |
| Public-header guard | `ay2d_check_no_bgfx_in_public_headers` CMake target (see §11.2) — fails CI if any `<bgfx/*.h>` / `<bx/*.h>` is reachable from `include/AY2D/**` | Phase 1 exit gate |
| Property-based (deferred) | `Test_TilemapRoundTrip.cpp` — random tilemap gen → dump tile ids → reload → bit-identical | Phase 5+ |

### 10.3 Testing discipline

Per [AYRenderer/design.md §14](../AYRenderer/design.md):

- Noop backend is **sticky** — once `Renderer::initialize(Backend::Noop)` runs, the process is locked to Noop.
- shaderc + multi-Renderer has known flakiness (exit 139 on Windows). Tests creating a second Renderer for 2D validation are forbidden in CI.
- `AYRenderer_Test` and `AYEntity_Test` baseline must remain green at 3× consecutive runs after each 2D PR.

---

## 11. Phase / Roadmap

| Phase | Goal | Deliverables | Exit gate |
|---|---|---|---|
| **Phase 0 — docs (this PR)** | Get everyone to sign off on boundaries. | `AY2D/design.md` chapters §0–§12 + Appendix A; `AYRuntime/AY2D/` directory contains **only** this `design.md`; no submodule added; no CMake entry. | Architecture review sign-off; capability map §E L72-85 wording reconfirmed; **no code merged**. |
| **Phase 1 — skeleton + first commit** | Empty submodule with header stubs. | Register submodule; minimal `include/AY2D/World2D.h` + `Tilemap.h` + `Sprite.h` + `OrthographicCamera.h`; CMake `add_subdirectory` gated behind `AY_ENABLE_AY2D` (default OFF). | Skeleton compiles in CI; `ENABLE_AY2D=OFF` build remains green; **no bgfx in public headers** (F-5 — see §11.2). |
| **Phase 2 — finite tilemap MVP** | Render a 32×32 finite tilemap with one atlas, no animation. | `.aytilemap` + `.ayatlas` formats (L1); `Tilemap` L2; `TilemapRenderSystem` (Present lane); Phoskia variant `tilemap.phoskia`; `TilemapParallaxDemo`. Load-failure contract: §11.3. | Demo plays in Editor viewport. |
| **Phase 3 — animation + flip + layers** | Per-tile animation table; sprite flip; layer sort. | Tile-animation runtime; 4 batch states tested. | 1 000 animated tiles ≤ 0.6 ms. |
| **Phase 4 — streaming** | Infinite / chunked map. | `TilemapChunkSource` interface; budget policy; telemetry. | Chunk IO ≤ budget; deterministic fallback on not-loaded tiles. |
| **Phase 5 — collision interface only** | `ITileCollisionQuery`. | No backend yet; doc lists backend candidates (§8.3). | Interface compiles; unit tests for cell lookup. |
| **Phase 6 — performance hardening** | Hit budgets in §10. | `DrawListBuilder` integration with new `Forward2DOpaque` key. | All budgets pass; visual regression tests green. |
| **Phase 7 — deferred** | Optional Sim-lane tile state, 2D backend selection, lockstep. | Gated on DET-01 + product ask. | Per-package sign-off. |

### 11.1 Phase 0 exit checklist (this PR's own gate)

- [x] `d:\Projects\AYRuntime\AY2D\design.md` written.
- [x] capability map §E L72-85 text pasted verbatim into Appendix A.
- [ ] Architecture review sign-off (Graphics lead, ECS lead, Tools lead).
- [ ] L-1..L-17 lock decisions each map to a specific section (Appendix B already maps them — sign-off is the human act).
- [ ] R-1..R-10 risks each have default position and resolution trigger.
- [ ] `AY2D/` directory contains **only** this `design.md`.
- [ ] Root `CMakeLists.txt` and `.gitmodules` unchanged.
- [ ] `AYRenderer`, `AYEntity`, `AYResource`, `AYPhysics`, `AYShader` source files unchanged.
- [ ] **Audit patches F-1..F-19 (P0/P1/P2) applied** — see §13 "Changelog / audit" at the bottom of this file.

### 11.2 Phase 1 guard target: `ay2d_check_no_bgfx_in_public_headers`

**Audit finding F-5** — the engine currently does NOT have an automated "no bgfx in public headers" guard. `AYRenderer/unittest/Test_PublicHeaderSurface.cpp` exists but is compile-only; it does not grep `<bgfx/bgfx.h>` and does not walk the include tree. AY2D will not inherit that gap.

Phase 1 must add a CMake custom target that fails CI when any header under `include/AY2D/**/*.h` includes `<bgfx/bgfx.h>` or `<bx/*.h>` (the bgfx companion headers):

```cmake
# CMakeLists.txt — Phase 1
add_custom_target(ay2d_check_no_bgfx_in_public_headers
    COMMAND ${CMAKE_COMMAND} -E echo "Scanning AY2D public headers for bgfx/bx leaks..."
    COMMAND ${CMAKE_COMMAND}
        -DAYTGT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include/AY2D
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoBgfxInPublicHeaders.cmake
    COMMENT "AY2D public-header bgfx leak guard"
    VERBATIM
)
# Hook the check into every AY2D build and into CI.
add_dependencies(AY2D ay2d_check_no_bgfx_in_public_headers)
```

The check script (`cmake/CheckNoBgfxInPublicHeaders.cmake`) greps `bgfx/bgfx.h`, `bgfx/bimg.h`, `bx/*.h` across all `*.h` files under `include/AY2D/` and exits non-zero on any match. It also fails if a public header transitively pulls in `<bgfx/...>` via a missing `PRIVATE` boundary — the check runs `clang -M -I include/AY2D` on every public header and asserts the resulting dependency tree contains no `bgfx` path. The clang-scan step is the mechanical version of the manual `Test_PublicHeaderSurface` compile-test.

This guard target is the **Phase 1 exit gate** (added to §11.1 row "Skeleton compiles in CI"). It must run on every PR that touches `include/AY2D/**` and on every nightly build.

### 11.3 Phase 2 load failure path (F-18)

`.aytilemap` / `.ayatlas` may fail to load for many reasons: missing file, corrupt header, version mismatch, atlas texture missing, invalid tile-id range. The contract:

1. `AYResourceManager::load<IAYTilemap>(path)` returns a **strong-cache failure marker** (an `IResource` whose `isValid()` returns `false`); it does NOT throw.
2. `TilemapRenderSystem` checks `TilemapComponent::tilemapResource.isValid()` each frame and skips submission when invalid; the entity draws nothing rather than crashing.
3. ECS-side: `TilemapComponent::loadState` enum (`Unloaded / Loading / Loaded / Failed`) is exposed for the editor inspector.
4. GPU-side: any partially-uploaded atlas texture is freed via `RenderResourceManager::destroyAtlas(handle)` on the failure path; no leak.
5. Logs: a single stderr line on first failure per resource path (debounced — no per-frame spam).
6. **No fallback tilemap is auto-substituted.** A failed tilemap stays invisible until the file is fixed on disk and reloaded via hot-reload. This matches `AYResource/design.md §6.4` "missing dependencies → log + placeholder, never crash" — placeholder = invisible, not crash.

---

## 12. Risks & open questions

| ID | Risk | Default position | Resolution trigger |
|---|---|---|---|
| R-1 | Capability map §E L72-85 text confirmed verbatim in Appendix A. | Doc treats §E L72-85 as authoritative; Appendix A pastes the original text verbatim; diff against live doc is a Phase 0 sign-off review step. | Phase 0 sign-off — ask the owner of `AYRuntime/docs/first-game-engine-capability-map.md` to confirm §E L72-85 wording. (F-19: status upgraded to "已核实" once Appendix A pasted.) |
| R-2 | `AYPhysics` 2D backend is "TBD". Doc must not pick one prematurely. | Interface-only; candidates = Jolt-2D / Box2D / Chipmunk2D / custom det subset (§8.3). | Product or team decision in Phase 5; a separate decision document, not a default in this design. |
| R-3 | `RenderPassSlot` is **append-only**. 2D needs a new pass slot `Forward2DOpaque`. | Reserve `RenderPassSlot::Forward2DOpaque` in the AY2D design; do not modify `AYRenderer` source in this Phase 0. | Phase 1+ PR adds the slot. |
| R-4 | `DrawItem` does not accept 2D fields. Adding them must go through the existing public type pattern. | Use `DrawItem::payload` extension (mirrors `DrawItem::boneMatrices` pattern in [ENGINE-FOUNDATION-PLAN §5.7](../../ENGINE-FOUNDATION-PLAN.md)); 2D payload as `const DrawPayload2D*`. | Phase 2 PR adds the `DrawPayload2D` type to AYRenderer's public header (append-only). |
| R-5 | Texel-center / gutter / nearest-vs-bilinear rules are easy to mis-state. Several internet sources conflate them. | Document locks the matrix in §5.3 with no equivalence claims; §5.4 makes the 4-tap / 9-tap distinction explicit. | Phase 0 review by a graphics engineer. |
| R-6 | "9-tap vs 4-tap" — there is no hardware-level 9-tap. Saying "9-tap bilinear" is wrong. | Use exact wording: "9-tap weighted filter (custom shader); **not** equivalent to hardware bilinear"; §5.4 locks this. | Phase 0 review. |
| R-7 | Streaming tilemap + lockstep is dangerous. | Streaming is Present-only; Sim lane sees a deterministic tile view (default id for not-loaded). §6.3 locks this. | Phase 4 review. |
| R-8 | `.aytilemap` and `.ayatlas` are new L1 types; L1/L2/L3 layering must be respected. | Doc locks the layering in §9 explicitly. | Phase 2 review. |
| R-9 | AYUI is forbidden (capability-map §E L85). But 2D's `OrthographicCamera` view might still be composed into the Editor viewport, which uses AYUI. | Reading is fine; depending on AYUI is not. Editor glue lives in `AYEditor`, not `AY2D`. | Phase 2 review. |
| R-10 | "Sprite animation" overlaps with `AYAnimation` (designed for skeletal per [ENGINE-FOUNDATION-PLAN §5.4](../../ENGINE-FOUNDATION-PLAN.md)). | Tile/sprite animation uses AY2D's own table (no skeletal); reuse `AYTime` only. `AYAnimation` is **not** a dependency. | Phase 0 review. |
| R-11 | (F-13) DET-01 (`ayt::Fixed32`) is the gate for "custom deterministic subset" backend in §8.3. Its current shipping status is **not traced** here. | When Phase 5 sign-off arrives, AY2D's design review must include a one-line trace of `ENGINE-DETERMINISM-ARCHITECTURE.md §6` "DET-01 status". Until then, the "custom det subset" row in §8.3 is marked "Gated on DET-01; do not start before DET-01 ships". | Phase 5 PR. |

---

## Appendix A — capability map §E L72-85 (verbatim)

> Pasted from `d:\Projects\AYRuntime\docs\first-game-engine-capability-map.md`. Reviewers should compare line-for-line with the live doc at PR sign-off; if the live doc has drifted, the design must be updated.

```
## E. Presentation — World 2D (not AYUI)

| CapId | Game part | Needed engine capability | Own | Suggested AY surface | GapStatus |
|-------|-----------|--------------------------|-----|----------------------|-----------|
| W2D-01 | 房间场景（厨房/大厅/情绪房） | Scene load/switch, orthographic camera | Engine | new **AY2D** / World2D pass | |
| W2D-02 | 厚实 2D 背景与角色精灵 | Sprite batch + layers/z-order | Engine | AY2D + AYRenderer pass | |
| W2D-03 | 小人走动、进房间交互 | Movement + trigger volumes | Engine | AY2D; optional thin physics | |
| W2D-04 | 瓦片可行走/碰撞（可选） | Tilemap collide/nav layer | Engine | AY2D tilemap (P2 for this game) | |
| W2D-05 | 哥特光影/暗角/烛光 | 2D lights or post FX | Engine | AYRenderer FX (later) | |
| W2D-06 | 锚点低时噪点/闪回表现 | Post/full-screen FX hooks | Engine | renderer FX API; when Game triggers | |
| W2D-07 | 料理烟雾/矿物光泽 VFX | Sprite/particle FX | Grey | basic sprites Engine; look Game | |

**Constraint (do not violate):** World tiles/sprites are **not** built on AYUI widgets.
```

(If the live doc has changed since this design was written, paste the current text here and re-confirm the L-1..L-17 / R-1..R-10 / §5 / §8 locks remain consistent.)

---

## Appendix B — Lock-decision → section index

| Lock | Section |
|---|---|
| L-1 | §2.1 |
| L-2 | §2.1 |
| L-3 | §2.2, §3, §4 |
| L-4 | §2.2, §4, R-9 |
| L-5 | §4.1, §7.1, R-4 |
| L-6 | §1, §4.2, R-3 |
| L-7 | §5.1 |
| L-8 | §5.2, §5.5 |
| L-9 | §5.3 |
| L-10 | §5.4, R-6 |
| L-11 | §6.3, R-7 |
| L-12 | §8.3, R-2 |
| L-13 | §9.1, §9.2, R-8 |
| L-14 | §11.1 |
| L-15 | §4.1 |
| L-16 | §2.2, §3.1, §4.1 |
| L-17 | §10.1, §10.2, §10.3 |

---

*End of AY2D Phase 0 design. No code merged.*

---

## 13. Changelog / audit log (F-9)

This design document is the source of truth for AY2D's architecture. Every change to the design is recorded here. Code-level changes go in the per-PR commit message; design-level changes go here.

### 13.1 v0.1 — 2026-07-27 (Phase 0 initial + industrial-grade review)

**Initial draft** by Claude (2026-07-27): all chapters §0–§12 + Appendix A/B written. Phase 0 exit criteria satisfied for `AY2D/` directory contains only this `design.md`; no source files; no submodule registration; no CMake entry; root `CMakeLists.txt` / `.gitmodules` unchanged.

**Industrial-grade audit patches** applied in this same commit:

| ID | P | Section | Patch |
|---|---|---|---|
| F-1 | P0 | §6.1 + §9.1 | Tile-id storage width unified: `TileIdPackMode { Narrow16 default, Wide32 }`. Loader refuses mode mismatch. |
| F-2 | P0 | §7.4 | Corrected `std::sort` wording to `std::stable_sort` (audit found original phrasing conflated C++17). |
| F-3 | P0 | §11.1 | Phase 0 exit checklist rebalanced: items already true (capability-map paste) marked `[x]`. |
| F-4 | P0 | §6.2 + §8.1 | Stub types `ChunkRequestHandle` / `Ray2D` / `RaycastHit2D` added; `CollisionFlags` bitwise operators declared; `Empty` vs `None` semantics clarified. |
| F-5 | P0 | §11.1 + §11.2 | Phase 1 exit gate adds `ay2d_check_no_bgfx_in_public_headers` custom target (audit found no existing automated bgfx-leak guard). |
| F-6 | P0 | §4.2.1 | "Cross-module PR ownership" table added (audit found no documented ownership for Phase 2 cross-module changes). |
| F-7 | P1 | §10.1 | Reference hardware SKU named (i7-12700 + RTX 3060 + 32 GB DDR4 + Win11). |
| F-8 | P1 | §10.1.1 | Profiling counter naming convention declared (snake_case `ay2d_<metric>_<unit>`); documented as module-local, NOT global; transparency note on inconsistency with `FrameStats` CamelCase. |
| F-9 | P1 | §13 | This changelog section. |
| F-10 | P1 | §3.3 | ECS lane mapping table added; documented that `SystemLane` is prose-only (DET-04 deferred); `register2DSystem<T>(priority)` helper mirrors existing `registerAnimationSystem()` pattern. |
| F-11 | P1 | §6.2 + §6.2.1 + §10.2 | Hot-reload policy inherits `AYHotReloadWatcher` whole-second mtime; no version stamps; `Test_HotReload_Tilemap.cpp` added to test matrix. |
| F-12 | P1 | §8.1 | CollisionFlags `Empty` vs `None` semantics locked in `flagsAt` / `isBlocked` defaults. |
| F-13 | P1 | §8.3 + §12 R-11 | DET-01 trace added; "custom det subset" row marked "gated on DET-01" with Phase 5 sign-off trace requirement. |
| F-14 | P2 | §1 / §3 / §3.4 | `version counter` → `resourceEpoch`, semantically narrowed. |
| F-15 | P2 | §5.5 | `AtlasDesc` gains `TextureWrap { Clamp, Repeat, Mirror }` per-axis. |
| F-16 | P2 | §6.2 | `TilemapBudget` gains `EvictionPolicy { LRU default, Distance, TimeWindow }`. |
| F-17 | P2 | §10.2 | `Test_HotReload_Tilemap.cpp` added to test categories (also covers F-11). |
| F-18 | P2 | §11 Phase 2 | Load failure path contract locked (no-throw; `isValid()` false marker; `loadState` enum; GPU resource cleanup; debounced log; no auto-fallback). |
| F-19 | P2 | §12 R-1 | R-1 status upgraded to "已核实" once Appendix A pasted verbatim. |

### 13.2 v0.1.1 — 2026-07-27 (review consistency fixes)

Design-review patches (no code):

| ID | Section | Patch |
|---|---|---|
| C-1 | §0 north-star | Softened: Present-lane walk on `ITileCollisionQuery` now; kinematic + heightmap physics deferred to Phase 5+. |
| C-2 | §2.3 + §4 matrix | Removed `AYAnimation` from allowed deps; marked forbidden (aligns with R-10). |
| C-3 | §9.4 | Animation accumulator locked to integer-ms remainder (matches §7.2). |
| C-4 | §11 | F-18 load-failure contract moved out of the roadmap table into §11.3. |

### 13.3 Future versions (template)

When this file is updated, append a new section here:

```
### 13.X vX.Y — YYYY-MM-DD (<PR title>)

**Phase**: <0..7>
**Locked changes**:
- <section>: <one-line summary>

**Open follow-ups**:
- <one-line summary>
```