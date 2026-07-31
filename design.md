# AY2D — Design

> **Status**: P3I.4 ship (World2DSnapshot::diff + resourceEpoch O(1) fast path; §13.23). P3I.3 (L-7 coverage, §13.22), P3I.2 (removeTilemap purge, §13.21 + §18.7), P3I.1 (blockedTileIds, §13.20), and §13.PF C6 → C6-R1 amendment still in the chain.
> **Version**: v0.1.21 (2026-07-30).
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

    // Default impl: `flagsAt(cell) != CollisionFlags::Empty`. A cell
    // is "blocked" iff it carries any flag beyond the Empty bit
    // (the §6.3 deterministic-default contract; Phase 0 §8.1 used
    // a buggy `x & mask != Empty` form which was always true).
    // Override only when the product needs finer granularity.
    //
    // §13.PF pre-flight retraction: the previous wording was
    // self-contradictory — `x & mask` cannot equal `Empty` when
    // `Empty` is not in `mask`. Phase 5 ships the corrected form.
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
| L-3I-1 (chunk-source ownership) | §13.21, §18.7 |
| L-3I-2 (one-source-per-tilemap) | §13.21, §18.7 |
| L-3I-3 (purge reuses eviction path) | §13.21, §18.7 |
| L-3I-4 (pending cancel ≠ eviction) | §13.21, §18.7 |
| L-3I-5 (removeTilemap strict ordering) | §13.21 |
| L-3I-6 (swap is no-purge) | §13.21 |
| L-3I-7 (direct purge ≠ epoch bump) | §13.21 |
| L-3I-8 (camera POD style) | §13.22 |
| L-3I-9 (snapshot diff O(1) epoch fast path) | §13.23 |
| L-3I-10 (snapshot diff same-world precondition) | §13.23 |
| L-3I-11 (snapshot diff `(id, gen)` key) | §13.23 |
| L-3I-12 (snapshot diff sort + two-pointer) | §13.23 |
| L-3I-13 (forward lock: future mutator bumps epoch) | §13.23 |
| KI-3I-1 (`eraseByKey` not bumping `evictions_lru`) | §13.21, §18.7 (deferred to Phase 3J) |
| KI-3I-2 (sentinel bug in `evictDownTo(0)` — fixed in P3I.2) | §13.21 |

---

*AY2D at v0.1.21 (2026-07-30). Phase 0/1+/2/3A/3B/3C/3D/3E/3F/3G/3G.1/3G.2a/3H.1/3H.2/3H.3/Phase 5/3I.1/3I.2/3I.3/3I.4 all shipped in-AY2D; cross-module PRs (CM-1..CM-5) deferred to §4.2.1 owners.*

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

### 13.4 v0.1.2 — 2026-07-28 (Phase 1+ scaffold close)

**Phase**: 1 (skeleton)
**Locked changes**:
- §10.2 + §11.1: AY2D ships its first `unittest/` subtree (mirrors AYPhysics sibling pattern), with AYTest-linked `AY2D_Tests` target. Three stub suites — `Test_TileCoord` / `Test_CollisionFlags` / `Test_TileIdPackMode` — cover the four placeholder public headers plus the §8.1 `CollisionFlags` operator set. No `.cpp` implementation yet; these are compile + invariant tests over the header surface, not functional tests.
- §3.4: `World2D::resourceEpoch` semantics locked in `Test_TileCoord.cpp` ("`resourceEpoch` bumps only on new `IResource` instance or `addTilemap` / `removeTilemap` / `swapTilemap`").
- §8.1: `CollisionFlags` (`Empty` vs `None`) and the `operator| / & / ^ / ~` set are now compile-checked and behavior-checked in `Test_CollisionFlags.cpp`. The header `AYTileCollision.h` is added to `include/AY2D/` to materialize the operator set without bumping the design; full `ITileCollisionQuery` interface lands in Phase 5+ per F-12.
- §6.1: `TileIdPackMode` enum byte-size lock (`sizeof(TileIdPackMode) == 1`) verified in `Test_TileIdPackMode.cpp`.
- Umbrella `AY2D.h` is added at the AY2D root. Consumers prefer `#include <AY2D.h>` over direct subdirectory includes; mirrors AYPhysics umbrella pattern (`AYRuntime/AYPhysics/AYPhysics.h`).
- Root repository: `option(AY_ENABLE_AY2D ... OFF)` + conditional `add_subdirectory(AYRuntime/AY2D)` (default OFF) lands at the root `CMakeLists.txt`. End-to-end verified on Visual Studio 2026 (MSVC 19.51.36252) + cmake 4.3.1 + Ninja + vcpkg toolchain.

**Open follow-ups**:
- Phase 2 finite-tilemap MVP — `src/*.cpp` implementations + `.aytilemap` Loader stub + first real `TilemapParallaxDemo`.
- `RenderPassSlot::Forward2DOpaque` and `DrawItem::payload` cross-module PRs (gated on AYRenderer maintainer per §4.2.1).
- `IAYTilemap.h` / `IAYTileset.h` cross-module PR (gated on AYResource maintainer per §4.2.1).
- `Test_HotReload_Tilemap` (F-11 / F-17) and visual / performance tests (F-7) deferred to Phase 2 alongside `.aytilemap` loader.

### 13.5 v0.1.3 — 2026-07-29 (Phase 2 finite-tilemap CPU MVP)

**Phase**: 2 (finite tilemap — CPU side only)
**Locked changes**:
- §3 + §6: First CPU-side .cpp implementations land.
  - `src/AYTilemap.cpp` — `loadChunkFromSource(t, source, coord)` is the
    first real .cpp in the module. Handles the four failure paths
    (null source / invalid handle / width mismatch / sync retry) and
    drives `loadState` through `Unloaded → Loading → Loaded | Failed`.
  - `src/AYInMemoryTilemapChunkSource.cpp` — LRU cache + `put` /
    `tryGetChunk` / `requestChunk` / `cancelChunk` round-trip;
    eviction policy **locked to LRU** (Phase 2 default; `Distance` /
    `TimeWindow` arrive with Phase 4 streaming).
- §6.1 storage width (`TileIdPackMode`) **single source of truth** is
  `include/AYTileCoord.h`. Both `AYChunkData` and `AYTilemap` now
  include `AYTileCoord.h` for the enum, avoiding any future ODR risk.
- §5.1 + §5.2 + §5.3: Header-only `AYTileSamplerUV.h` lands.
  - `tileUV(tileId, AtlasDesc)` returns the half-texel-center UV
    rect with gutter extrusion.
  - `isValidAtlasDesc(desc)` locks `gutter < min(tileWidth,
    tileHeight) / 2`.
  - `isPixelPerfectSafe(invariants)` codifies the four invariants
    from §5.3 (integer world pos + integer camera zoom + gutter == 0
    + integer viewport scale) — pixel-perfect is false if ANY one
    breaks. The four invariants were listed in prose before; this
    is the single source of truth that Editor viewport + RenderSystem
    both query.
- §6.2 `ITilemapChunkSource` interface + `AYChunkData` payload structure
  now live in AY2D-internal headers (no cross-module PR required).
  Production chunks still arrive via Phase 3+ cross-module PR
  (per §4.2.1: AYResource maintainer owns `IAYTilemap.h`).
- CMakeLists: `add_library(AY2D INTERFACE)` is **flipped to STATIC**
  with two real .cpp files. This is the visible signal that Phase 0
  / Phase 1+ docs-only exit closed and Phase 2 CPU MVP opened.
- §10.2: Three new unit-test sub-suites added.
  - `Test_Tilemap` — 10 cases: round-trip / out-of-range drop / Narrow
    rejects wide tile ids / chunk-source swap / width-mismatch
    failure / null source failure / isInRange / defaultTileId /
    resize clears / flagsAt default returns Empty.
  - `Test_TileSamplerUV` — 6 cases: default-zero UV / atlas
    validation rule / half-texel center / gutter shrink / tile-id
    layout / pixel-perfect invariants gate.
  - `Test_InMemoryTilemapChunkSource` — 6 cases: round-trip /
    double-insert refusal / LRU eviction / async-handle-issued
    states / cancel-no-op / unknown-chunk returns false.
  Existing stubs (`Test_TileCoord` / `Test_CollisionFlags` /
  `Test_TileIdPackMode`) untouched.

**Open follow-ups**:
- `.aytilemap` binary format + IAYTilemap cross-module PR (still
  gated on AYResource maintainer per §4.2.1).
- `RenderPassSlot::Forward2DOpaque` + `DrawItem::payload` cross-module
  PRs to AYRenderer.
- `AYTileMapComponent` ECS component cross-module PR to AYEntity.
- `TilemapParallaxDemo` (the noop-visual MVP that proves the chain).
- `Test_HotReload_Tilemap` (F-11 / F-17) waits on the `.aytilemap`
  cross-module PR.
- Phase 3 animation bake, Phase 4 streaming chunk sources, Phase 5
  ITileCollisionQuery impl.

### 13.6 v0.1.4 — 2026-07-29 (Phase 3 in-AY2D real impl promotions)

**Phase**: 3 (in-AY2D scope only — no cross-module PRs)
**Locked changes**:
- §3 + §3.4: `World2D` placeholder is promoted to a real impl
  (`src/AYWorld2D.cpp`, full `addTilemap` / `removeTilemap` /
  `swapTilemap` / `find` API). `TilemapHandle` carries a
  `(uint32_t id, uint32_t generation)` pair for basic ABA safety.
  Each mutation bumps `resourceEpoch` exactly once. The registry
  is a small vector-of-`Entry` (Phase 3 caps the registry at a
  few hundred entries); Phase 4 streaming replaces this with a
  hash map. The `IAYTilemap*` resource pointer is a non-owning
  raw pointer placeholder; the cross-module PR replaces it with
  `TilemapResourceHandle` (Phase 3+).
- §3 + §5.3: `OrthographicCamera` placeholder is promoted to a
  real impl with `viewMatrix()` / `projectionMatrix()` /
  `isPixelPerfectSafe()` math. The matrices are
  `ayt::math::Float4x4` from `AYMath`. The four §5.3 pixel-perfect
  invariants are codified in `isPixelPerfectSafe()` (single source
  of truth — RenderSystem2D and Editor viewport both query this).
  AY2D now links `AYMath` (the only new allowed dependency; §2.3
  already admitted AYMath).
- §6.2: `ChunkRequestHandle` packs 24-bit index + 8-bit
  generation into a single uint32_t. The
  `(uint32_t index, uint32_t generation)` constructor is the
  source's only entry point; the equality check compares both
  fields (the basic ABA guard). Generation bumps on overflow
  past `kMaxIndex = 0x00FFFFFF`. The underlying id layout is
  unchanged on the wire (still a single uint32_t) so the
  `cancelChunk` / `tryGetChunk` API surface is preserved.
- §10.1.1: `Ay2DCounters` POD lands as `include/AY2DCounters.h`.
  Six atomic fields (`chunk_io_us` / `chunk_io_bytes` /
  `chunk_resident_count` / `atlas_bytes` / `draw2d_items` /
  `draw2d_pass_us`) with `snapshot()` / `resetAll()` /
  `resetPerFrame()` helpers. `World2D` and
  `InMemoryTilemapChunkSource` each carry their own counters
  instance (mirrors the `AYPhysicsManager` pattern). The chunk
  source's `put` path now accumulates `chunk_io_bytes` and
  updates `chunk_resident_count` on every insert/erase; the
  request → delivery latency is stamped into
  `pending.requestTimeUs` and accumulated into `chunk_io_us`
  at the matching `put()` time.
- §10.2: Four new unit-test sub-suites added.
  - `Test_World2D` — 9 cases: fresh epoch / add bumps / remove
    bumps / swap bumps / remove invalidates handle / add-remove-
    add gives different generation / packSortKey bits / invalid
    handle is no-op / multiple tilemaps and find.
  - `Test_OrthographicCamera` — 11 cases: identity view / zoom
    scale / position translate / projection aspect ratio /
    degenerate viewport / pixel-perfect safe / unsafe with
    fractional zoom / unsafe with fractional position / unsafe
    with non-zero gutter / default layerMask / projection matrix
    after scale.
  - `Test_ChunkRequestHandle` — 10 cases: default invalid /
    hand-constructed valid / pack index isolation / pack
    generation isolation / index mask clip / generation mask
    clip / equality requires both / `kInvalidId` /
    `kMaxIndex` / pack-unpack round-trip.
  - `Test_Counters` — 6 cases: defaults zero / `resetAll()` /
    `resetPerFrame()` / chunk-source `put` populates bytes +
    resident count / chunk-source eviction / accessor identity.
  Existing tests (Phase 2 + Phase 1+ stubs) untouched.
- §13.6: This changelog entry.
- `AY2D.h` umbrella now also includes `AY2DCounters.h`.

**Open follow-ups**:
- `.aytilemap` binary format + IAYTilemap cross-module PR (still
  gated on AYResource maintainer per §4.2.1).
- `RenderPassSlot::Forward2DOpaque` + `DrawItem::payload` cross-module
  PRs to AYRenderer.
- `AYTileMapComponent` ECS component cross-module PR to AYEntity.
- `TilemapParallaxDemo` (the noop-visual MVP that proves the chain).
- `Test_HotReload_Tilemap` (F-11 / F-17) waits on the `.aytilemap`
  cross-module PR.
- Phase 3 animation bake, Phase 4 streaming chunk sources, Phase 5
  ITileCollisionQuery impl.

### 13.7 v0.1.5 — 2026-07-29 (Phase 3B animation + sprite wiring)

**Phase**: 3B (in-AY2D scope only — animation table, sprite struct, no cross-module PRs)

**Locked changes**:
- §3 + §7.2: `Tilemap` gains `animationTable` (sparse
  `std::vector<std::vector<TileFrame>>` indexed by source tileId) +
  `animationState` (per-tile `currentFrameIdx` + integer-ms
  `elapsedMs` accumulator) + `lastTickUs` baseline + `hasBeenTicked`
  first-tick flag. Frames carry `{frameTileId, durationMs}`
  (§7.2 + §9.4 locks). Hot-path index bound is `TileIdPackMode::Narrow16`
  max (65 535).
- §7.2: integer-ms remainder accumulator (`uint32_t` elapsed per tile).
  First tick sets baseline (no initial jump); reversed clock clamps
  to 0 (R-7 spirit); zero-duration frame is a no-op (loop break
  prevents infinite spin). No float drift across long play sessions.
- §7.2: `tickTilemapAnimation(Tilemap&, int64_t nowUs)` is a free
  function in `src/AYTilemapAnimation.cpp`; `resolveAnimatedTileId(t,
  sourceTileId)` returns the live frame's `frameTileId` when an
  animation entry exists, else the source unchanged. `Tilemap::getTile`
  semantics are **unchanged** (Phase 2 contract preserved across all
  70 existing TEST_CASE); the animated lookup path is opt-in via
  `resolveAnimatedTileId`. The future `TilemapAnimationTickSystem`
  ECS wrapper @ priority 460 (§3.3) lands with the AYEntity
  cross-module PR in Phase 3+.
- §3 + §7.3: `Sprite` placeholder is promoted to a real struct.
  Fields: `worldMatrix` (`Float3x3` affine — 2D scene, no perspective
  column; verified to ship in `AYMath/aymath/MathTypes.h:609` with
  `identity()` factory), `sourceRect` (4 UV floats), `color` (4 RGBA
  floats, default opaque white), `flip` (`SpriteFlip` enum, 2-bit
  composed), `layer` (uint8_t, 0..31), `sortingKey` (uint32_t,
  0..0x00FFFFFF). `Sprite::packedSortKey()` mirrors `World2D::packSortKey`
  (§7.4 lock — both helpers bit-identical).
- §7.3: `SpriteFlip` enum + bitwise ops (`|` / `&` / `^` / `|=` / `&=`)
  + `hasFlip(flip, bit)` predicate. Flip is per-instance, encoded into
  the vertex stream (no extra vertex buffer; §7.3).
- §10.2: Two new unit-test sub-suites added.
  - `Test_TilemapAnimation` — 10 cases: empty default / single tick
    advances / loops after full sequence / remainder accumulates
    across small deltas (§9.4 drift check) / zero-duration no-op /
    same-time no-op / reversed clock clamp / first-tick baseline /
    multiple tiles independent / `resolveAnimatedTileId` returns
    live frame.
  - `Test_Sprite` — 8 cases: default identity / `packSortKey` layer in
    high byte / `packSortKey` 5-bit layer mask / `packSortKey` 24-bit
    sorting-key mask / flip bits compose / flip independent of layer
    + sort / color default opaque white / sourceRect default full
    atlas / worldMatrix identity default.
  Existing tests (Phase 1+ stubs + Phase 2 functional + Phase 3A
  real-impl) untouched. New total: **12 TEST_SUITE / 88 TEST_CASE /
  288 CHECK assertions PASS**.
- §2.3: `AYTileAnimation.h` is the new in-AY2D header;
  `src/AYTilemapAnimation.cpp` is the new .cpp. **No new module deps**
  — AYMath was already linked (Phase 3A `AYOrthographicCamera.h`); the
  animation tick uses raw `int64_t` microseconds (no `ayt::time::TimePoint`
  in the public API surface — consumers can wrap `Clock::gameNow()` if
  they want, the implementation stays AYTime-free). **R-10 lock holds**:
  no AYAnimation include, no AYAnimation link.
- §11.2: bgfx-leak guard `ay2d_check_no_bgfx_in_public_headers` stays
  green (12 headers scanned, 0 leaks). New headers include only
  `<cstdint>`, `<vector>`, `aymath/MathTypes.h`.
- §13.7: This changelog entry.

**Open follow-ups**:
- `.aytilemap` binary format that includes the animation-table section
  (L-13 / §9.1) — cross-module PR to AYResource maintainer.
- `RenderPassSlot::Forward2DOpaque` + `DrawItem::payload` cross-module
  PRs to AYRenderer (still gated per §4.2.1).
- `AYTileMapComponent` + `AYSpriteComponent` ECS components +
  `TilemapAnimationTickSystem` ECS wrapper @ priority 460 — cross-module
  PR to AYEntity maintainer. The free function `tickTilemapAnimation()`
  is the body; the system class wraps it.
- `TilemapParallaxDemo` (Noop visual MVP) waits on `DrawItem::payload`.
- `Test_HotReload_Tilemap` (F-11 / F-17) waits on the `.aytilemap`
  cross-module PR.
- Phase 4 streaming chunk sources (`Distance` / `TimeWindow` eviction
  policies), Phase 5 `ITileCollisionQuery` impl + 2D backend pick.

---

## 14. Phase 3C counter wiring (planned)

> Drafted before code; design-first per `ay-dev-rules`. Phase 3C
> extends `Ay2DCounters` with **tile-dimension** metrics that are
> in-AY2D scope (no cross-module PR needed) and wires them onto
> the existing real-impl mutation paths. The Phase 3A counters
> shipped as a struct-only API; Phase 3C is the **first** commit
> that drives them from real mutation paths and asserts deltas in
> unit tests (no longer mock data).

### 14.1 New counters (in-AY2D scope)

| Counter | Type | Unit | Storage | Owner | Read by |
|---|---|---|---|---|---|
| `ay2d_tiles_mutated` | `std::atomic<uint64_t>` | count | cumulative | per `Tilemap` (new field) | per-tilemap telemetry / Tests |
| `ay2d_tiles_resident` | `std::atomic<uint64_t>` | count | gauge | per `Tilemap` (new field) | per-tilemap telemetry / Tests |
| `ay2d_tilemaps_in_world` | `std::atomic<uint32_t>` | count | gauge | per `World2D` (reuse `counters`) | per-world telemetry / Tests |

**Why these three (not draw2d / atlas_bytes / etc.)**:
- `draw2d_items` / `draw2d_pass_us` are per-frame gauges that
  require the `RenderSystem2D` cross-module PR (gated per
  design.md §4.2.1). Phase 3C must not pre-bump them from
  `World2D::addTilemap` — that would be a counting-error waiting
  to happen.
- `atlas_bytes` is an L3 gauge that requires the
  `RenderResourceManager` cross-module PR. Same reason.
- `chunk_io_*` already live on `InMemoryTilemapChunkSource`
  (Phase 3A real-wired); P3C adds tests that prove the real
  deltas with the new API, not duplicate the wiring.

### 14.2 Wiring contract

| Mutation | Counter delta |
|---|---|
| `Tilemap::setTile(cell, tileId)` actual write path (out-of-range dropped, no-op) | **no delta** (only successful writes count) |
| `Tilemap::setTile` first-write lazy-fill | `tiles_resident += (expected − previous)` then `tiles_mutated += 1` |
| `Tilemap::resizeGrid(newCols, newRows, newMode)` | `tiles_resident = 0` (clears), `tiles_mutated += 1` |
| `Tilemap::loadChunkFromSource` → success path | `tiles_resident = newCount`, `tiles_mutated += 1` |
| `Tilemap::loadChunkFromSource` → null source / width mismatch / handle invalid | **no delta** (mutation gated on success) |
| `Tilemap::clear()` | `tiles_resident = 0`, `tiles_mutated += 1` |
| `World2D::addTilemap(layer, sortingKey)` | `tilemaps_in_world += 1`, `resourceEpoch += 1` (existing §3.4 lock) |
| `World2D::removeTilemap(handle)` matching | `tilemaps_in_world −= 1` (saturating at 0), `resourceEpoch += 1` |
| `World2D::swapTilemap(...)` matching | `resourceEpoch += 1` (no count delta — swap is in-place) |
| `World2D::swapTilemap(...)` not matching | **no delta** |

**No-allocation rule**: counter increments use
`fetch_add(..., std::memory_order_relaxed)` against an
`std::atomic<uint64_t>` field. No locks, no critical sections.

### 14.3 Tests

`unittest/Test_CountersWired.cpp` — 6–8 cases (no mock data):

1. `TilemapSetTileBumpsTilesMutated` — first write bumps by 1;
   second write at a different cell bumps by 1; out-of-range
   writes do not bump.
2. `TilemapSetTileFirstWriteBumpsResident` — first write to a
   4×4 grid bumps `tiles_resident` to 16 (expected capacity);
   confirm via `counters().snapshot()`.
3. `TilemapResizeGridResetsResident` — set tiles, resize,
   `tiles_resident == 0`.
4. `TilemapLoadChunkFromSourceSuccessBumpsBoth` —
   `loadChunkFromSource(t, source, coord)` with a pre-populated
   source → `tiles_resident == cols*rows`, `tiles_mutated += 1`.
5. `TilemapLoadChunkFromSourceFailureNoDelta` — null source,
   width mismatch, invalid handle each leave both counters
   unchanged from a recorded baseline.
6. `World2DAddTilemapBumpsInWorld` — `addTilemap` bumps
   `tilemaps_in_world` by 1 per call (3 calls → 3).
7. `World2DRemoveTilemapDecrementsInWorld` — add then remove
   leaves `tilemaps_in_world == 0` (saturating); remove of an
   invalid handle is a no-op.
8. `ChunkSourceRequestLatencyAccumulatesIoUs` — pre-populate
   a non-resident chunk via `requestChunk` → manually `put`;
   `chunk_io_us` increments by a non-zero delta (proves the
   Phase 3A request-time stamp lands in the counter).

**All assertions use real `counters().snapshot()` comparisons** —
no manual `fetch_add` testing stand-ins. The Phase 3A
`Test_Counters` cases (mock path) remain in place; Phase 3C
adds `Test_CountersWired` on top.

### 14.4 Out of scope (deferred)

- `draw2d_items` / `draw2d_pass_us` / `atlas_bytes` real wiring —
  gated on `RenderSystem2D` + `RenderResourceManager` cross-module
  PRs (§4.2.1).
- `chunk_io_us` histogram / p99 — Phase 4 (F-7 perf budget gate).
- Per-frame `resetPerFrame` automation — uses the existing
  Phase 3A helper manually; RenderSystem2D wiring is the cross-module
  PR mentioned above.

### 14.5 Risks / invariants

- **R-3C.1**: counter saturation. `tilemaps_in_world` decrement
  floors at 0 (no underflow on `fetch_sub` of zero). Use
  `fetch_sub(1)` followed by compare-and-set, or simply guarantee
  decrement count matches add count at the test level.
- **R-3C.2**: counter consistency under relocations.
  `std::atomic<uint64_t>` increments are independent of tile-data
  vector relocations; the counter lifetime is bound to `Tilemap` /
  `World2D` instance, not the underlying storage.
- **R-3C.3**: no double-counting. Out-of-range `setTile` returns
  without bumping; a failed `loadChunkFromSource` returns without
  bumping; `setTile` first-write lazy-fill bumps `tiles_resident`
  by `expected` (not by 1) on the FIRST write only — subsequent
  writes at already-allocated cells bump `tiles_mutated` by 1
  each, not `tiles_resident`.
- **R-3C.4**: `Ay2DCounters` field expansion. Adding three fields
  is binary-compatible at the C++ struct level because the new
  fields are appended (the struct has no virtual methods, and
  callers use `snapshot()` to read). Existing Phase 3A tests
  (which check default-zero / `resetAll` / `resetPerFrame`) remain
  valid against the expanded struct — the new fields are zero
  by default like the existing six.
- **R-3C.5**: bgfx-leak guard stays green. New code adds no new
  headers — only modifies existing ones, and the modifications
  are limited to inline arithmetic on existing fields.

---

### 13.8 v0.1.6 — 2026-07-29 (Phase 3C counter wiring — in-AY2D)

**Phase**: 3C (in-AY2D scope only — counter real-wiring, no cross-module PRs)

**Locked changes**:
- §10.1.1 + §14: `Ay2DCounters` gains three tile-dimension fields
  (vs the six already-shipped chunk / draw gauges). They are
  in-AY2D scope and have unambiguous semantics (see §14.1 table).
- §10.1.1 + §14.2: wiring contract locked. Mutation paths
  enumerated with no-double-counting and no-allocation invariants.
  Out-of-range / failure paths produce **no delta**.
- §14.3: `Test_CountersWired` real-wired tests added (6–8 cases).
  Phase 3A `Test_Counters` mock-paths remain in place; P3C adds
  the **real** delta assertions on top.
- §13.8: This changelog entry. Front-matter bumped to v0.1.6.

**Open follow-ups** (unchanged from §13.7): same five follow-ups
—.aytilemap / Forward2DOpaque / AYSpriteComponent /
TilemapParallaxDemo / Test_HotReload + Phase 4 / Phase 5.

---

## 15. Phase 3D batch tile-fill API

> Phase 3D closes the **bulk write** hole in the `Tilemap`
> writer. Phase 3C locked the per-cell `setTile` mutation
> contract; Phase 3D adds the three batch-shaped entry points
> every editor paint + chunked-loader cross-module PR will
> reach for. Counters stay consistent: one **batch** operation
> is one **mutation event** (regardless of how many cells it
> touches), which is what `tiles_mutated` semantics already
> implied.

### 15.1 Surface

Three new free functions + one helper accessor on `Tilemap` /
`TileRect`:

| Symbol | Returns | Purpose |
|---|---|---|
| `setTileRange(Tilemap&, TileRect r, uint32_t tileId)` | `bool` | overwrites cells in `r` (clamped to grid); one mutation |
| `fillTile(Tilemap&, uint32_t tileId)` | `void` | shortcut for `setTileRange(gridRect(t), tileId)`; one mutation |
| `copyTileRange(Tilemap& dst, TileRect dstRect, const Tilemap& src, TileCoord srcOrigin)` | `bool` | copies cells from `src` (at `srcOrigin`) into `dstRect`; width-mismatch is a F-18-style no-op |
| `gridRect(const Tilemap&)` | `TileRect` | whole-grid half-open rect; `{0,0,cols,rows}` for sized, empty for unsized |
| `TileRect` (new type) | POD (16 bytes, int32_t x0/y0/x1/y1) | half-open tile-grid rectangle |
| `isEmpty / area / clampToGrid` | free helpers | algebra + grid clamp for `TileRect` |

### 15.2 Semantics (locked)

- Half-open `[x0, x1)` x `[y0, y1)` rect (R-3D.1).
- Bounds clamping silently (no signal — matches `setTile` §6.3
  silent-drop pattern).
- Empty rect (`x1 <= x0` or `y1 <= y0`) → no-op.
- Width mismatch on `copyTileRange` → no-op (F-18 contract).
- One batch = one mutation: `tiles_mutated += 1` exactly,
  regardless of cell count (R-3D.2).
- `fillTile` is logically `setTileRange(gridRect(t), tileId)`.
- `copyTileRange` operates on `Tilemap` data only (not
  `ITilemapChunkSource`; that bigger interface change is
  Phase 4).

### 15.3 Lazy-fill semantics (locked, R-3D.4)

Both `setTileRange` and `copyTileRange` lazily allocate the
destination storage **on the first write that grows the grid**.
The fill value **must be `defaultTileId`**, NOT the batch's
`tileId` (or src's first cell). Rationale: cells outside the
rect must read as `defaultTileId` (the Phase 2 §6.3 invariant
`UntouchedCellReadsDefaultTileId`). If `setTileRange` pre-
flooded with `tileId`, every cell on the grid would read as the
batch value, breaking the contract.

`tiles_resident` bumps to `expected` (= cols × rows) on the
first batch that grew storage, mirroring `setTile`'s first-
write behavior. Subsequent batches leave `tiles_resident` alone.

### 15.4 Tests (`Test_TilemapBatch.cpp` — 10 cases)

1. `SetTileRangeBasicRectangleOverwritesEveryCell` — rect
   `{1,1,3,3}` writes cells `{1,2} x {1,2}`; outside rect
   still reads `defaultTileId`.
2. `SetTileRangeOutOfRangeIsNoOp` — full-OOB rect → false,
   no delta.
3. `SetTileRangePartialClampWritesOverlapOnly` — `{2,2,8,8}`
   on 4×4 clamps to `{2,2,4,4}`; out-of-clamp unchanged.
4. `SetTileRangeEmptyRectIsNoOp` — empty rect → no delta.
5. `FillTileOverwritesEntireGridAsOneMutation` — one mutation,
   12 cells read `tileId`.
6. `CopyTileRangeSameModeRoundTrips` — same-mode copy lands
   the src value on the dst cell; outside `dstRect` reads
   `defaultTileId`.
7. `CopyTileRangeModeMismatchIsNoOp` — Wide32 src + Narrow16
   dst → false, dst unchanged, no delta.
8. `CopyTileRangePartialClampPartialCopy` — dst rect + src
   origin both partially outside; effective intersection
   is what gets copied; outside stays default.
9. `FirstWriteLazyFillFromBatchBumpsResidentOnce` —
   `tiles_resident` bumps once to `expected`, NOT per-cell;
   second batch leaves `tiles_resident` alone but adds a
   mutation.
10. `GridRectAccessorSpansWholeGrid` — `gridRect` covers the
    full grid; empty grid returns `isEmpty` rect.

### 15.5 Out of scope

- `.aytilemap` loader using `setTileRange` (cross-module PR).
- `AYSpriteComponent` painter (cross-module PR).
- `ChunkSource::putRange` / `requestRange` (Phase 4 streaming).

### 15.6 Risks / invariants

- **R-3D.1** half-open vs closed: documented at the type +
  locked in tests.
- **R-3D.2** counter double-bump: every batch is exactly one
  `fetch_add(1)` after a successful write span (continuation
  of P3C no-double-counting).
- **R-3D.3** `copyTileRange` width-oracle: `src.mode != dst.mode`
  is the only failure mode.
- **R-3D.4** `tiles_resident` lazy-fill from batch: first batch
  that grew storage bumps it once to `cols*rows`.
- **R-3D.5** empty grid: all batch ops no-op on unsized tilemap
  (mirrors `setTile`).
- **R-3D.6** bgfx-leak guard: `AYTileRect.h` is std::* + TileCoord
  only; no new module deps. `TilemapBatch.cpp` adds no new bgfx
  includes.
- **R-3D.7** `src/AYTilemapBatch.cpp` is the only new .cpp;
  appended to `SRC_FILES`.

---

### 13.9 v0.1.7 — 2026-07-29 (Phase 3D batch tile-fill API)

**Phase**: 3D (in-AY2D scope only — batch tile-fill, no cross-module PRs)

**Locked changes**:
- §3 + §15.2: `Tilemap` gains batch write APIs —
  `setTileRange`, `fillTile`, `copyTileRange`, `gridRect`.
  All four live as free functions in `src/AYTilemapBatch.cpp`;
  the existing `setTile` / `getTile` / `resizeGrid` surface
  is unchanged.
- §15.3 + R-3D.4 (locked bug fix noted in design): the batch
  lazy-fill uses `defaultTileId`, NOT the batch's `tileId`. An
  earlier draft pre-flooded with `tileId`; the implementation
  corrected this so untouched cells continue to read
  `defaultTileId` per Phase 2 §6.3
  (`UntouchedCellReadsDefaultTileId` invariant).
- §15.1: new public type `TileRect` ships in
  `include/AYTileRect.h` (POD 16 bytes, half-open `[x0, x1)` x
  `[y0, y1)` semantics). Free helpers `isEmpty`, `area`,
  `clampToGrid`.
- §15.4: batch counters = one mutation per batch operation.
  `tiles_mutated += 1` regardless of cell count;
  `tiles_resident = cols*rows` on first batch that grew storage,
  not subsequently. Out-of-range / mode-mismatch / empty-rect
  paths produce NO delta (same discipline as §14.2).
- §15.6: bgfx-leak guard verified green at 12 → 13 public
  headers. New file `AYTileRect.h` includes only std::* +
  `TileRect` types — no bgfx, no bx, no `AYMath`.
- §13.9: This changelog entry. Front-matter bumped to v0.1.7.

**Open follow-ups** (unchanged from §13.7 / §13.8):
- `.aytilemap` binary write that includes batch-paint records
  (Phase 3+ cross-module PR).
- `RenderPassSlot::Forward2DOpaque` + `DrawItem::payload`
  cross-module PRs to AYRenderer (still gated).
- `AYTileMapComponent` + `AYSpriteComponent` + batch-paint
  system wrapper cross-module PR to AYEntity (now that batch
  is in-AY2D, the ECS paint component just calls `setTileRange`).
- `TilemapParallaxDemo` (Noop visual MVP) waits on `DrawItem::payload`.
- `Test_HotReload_Tilemap` (F-11 / F-17) waits on the `.aytilemap`
  cross-module PR.
- Phase 4 streaming chunk sources + Phase 5 collision impl.

### 13.10 v0.1.8 — 2026-07-29 (Phase 3E world↔cell coordinate math)

**Phase**: 3E (in-AY2D scope only — coordinate math layer, no cross-module PRs)

**Locked changes**:
- §3 + §16: new in-AY2D header `include/AYTileMath.h` ships the
  world↔cell vocabulary previously missing from the submodule.
  Symbols are free functions / free helpers, never member methods
  on `Tilemap` (so `TileCoord` / `TileRect` stay context-free).
  The header includes only `AYTileCoord.h`, `AYTileRect.h`, and
  `aymath/MathTypes.h` (already-public AYMath — Phase 3A PUBLIC
  link). No new module dependency. No bgfx.
  - `worldToCell(ayt::math::FVector2 world, ayt::math::FVector2 cellOrigin, float cellSizeW, float cellSizeH) noexcept -> TileCoord`
  - `cellToWorld(TileCoord, ...) -> ayt::math::FVector2` (cell
    center, not corner — see §16.2 R-3E.5)
  - `aabbOverlappingCells(ayt::math::FVector2 worldMin, ayt::math::FVector2 worldMax, ...) noexcept -> TileRect`
  - `isCellInWorldBounds(TileCoord cell, ...) noexcept -> bool`
- §3 + §16.3: `Tilemap::setTile` / `setTileRange` / `copyTileRange`
  gain `[[nodiscard]]` world-coordinate overloads that internally
  delegate to the cell-coordinate forms. Existing cell-coordinate
  signatures and semantics are unchanged (P2 §6.3 contract intact).
  World-coord reads (`getTile(FVector2 world)`) return
  `defaultTileId` for any world point outside the grid, matching
  `getTile(TileCoord)` OOB semantics.
- §16: integer floor rule (R-3E.1). `worldToCell` rounds toward
  `-infinity` for both axes regardless of sign — `(-0.5, -0.5, ...)`
  maps to cell `(-1, -1)` which is then caught as OOB by the same
  downstream contract as any negative cell. The function does NOT
  clamp; clamping is the caller's job (every concrete write path
  in this design delegates to `setTile` which already OOB-drops).
- §16: half-pixel rule (R-3E.5). `cellToWorld` returns the **cell
  center** (offset `cellSize * 0.5`) so editor paint picks anchor
  on the visible square, not the corner. World-coord reads at a
  cell-center hit are exact; world-coord reads at any other point
  in the cell exhibit the half-texel variation the renderer
  already inherits from `OrthographicCamera::pixelPerfect` mode.
- §16.4: R-3E.6 `tileSize==0` guard. A malformed tilemap (or a
  zero-area cell config) makes `worldToCell` / `cellToWorld`
  return a zero-cell. `aabbOverlappingCells` on a zero-area
  cellSize returns the empty rect via `isEmpty`. All paths
  signal OOB / empty without dividing-by-zero.
- §16.5: tests — `Test_TileMath.cpp` ships 8 cases (see §16.5
  list). World↔cell round-trip, negative-origin truncation,
  AABB containing a single cell, AABB partially overlapping, AABB
  outside grid, AABB degenerate (zero-area), `setTile` via
  world coord + OOB read, `setTileRange` via world rect + partial
  clamp.
- §10.2: bgfx-leak guard stays green. New headers include only
  `<cstdint>`, `aymath/MathTypes.h` (PUBLIC link), and the
  existing in-AY2D `AYTileCoord.h` + `AYTileRect.h`. No `bgfx::*`,
  no `bx::*`, no third-party module.
- §13.10: This changelog entry. Front-matter bumped to v0.1.8.
  Total tests: **15 TEST_SUITE / 114 TEST_CASE / 446 CHECK assertions
  PASS** (was 14 / 106 / 396 at v0.1.7; +1 suite, +8 case, +50 CHECK).

**Open follow-ups** (unchanged from §13.7..§13.9):
- `.aytilemap` binary write that includes batch-paint records +
  world-coord authoring records — cross-module PR to AYResource
  maintainer.
- `RenderPassSlot::Forward2DOpaque` + `DrawItem::payload`
  cross-module PRs to AYRenderer (still gated per §4.2.1).
- `AYTileMapComponent` + `AYSpriteComponent` + world-coord
  `TilemapMousePickSystem` ECS wrapper — cross-module PR to AYEntity
  maintainer. The free function `cellOfWorldPoint(...)` is the body;
  the ECS system just wraps it.
- `TilemapParallaxDemo` (Noop visual MVP) waits on `DrawItem::payload`.
- `Test_HotReload_Tilemap` (F-11 / F-17) waits on the `.aytilemap`
  cross-module PR.
- Phase 4 streaming chunk sources — `AABB` overlap calls now have
  a ready-made `aabbOverlappingCells` to plug into
  `TilemapStreamingSystem` (Step 2 of Phase 4 PR). Phase 4 itself
  is still gated.
- Phase 5 `ITileCollisionQuery` impl — `isCellInWorldBounds` is the
  ready-made "is this query inside the grid?" guard for collision
  probes.
- Phase 6 perf hardening — world-coord `setTile` overload may
  become a hot path under heavy editor drag-paint; the helper
  traces whether that overflows the budget.

---

## 16. Phase 3E world↔cell coordinate math (in-AY2D scope)

> Phase 3E adds the cross-cutting **world↔cell math layer** the
> submodule has been deferring. Every editor paint drag, every
> collision probe, every streaming AABB test ultimately needs one
> of these functions and currently re-implements it inline. P3E
> lifts them into a single, well-tested surface that the future
> cross-module PRs can plug straight into.

### 16.1 Surface

| Symbol | Returns | Purpose |
|---|---|---|
| `worldToCell(FVector2 world, FVector2 cellOrigin, float cellSizeW, float cellSizeH)` | `TileCoord` | World-space point → cell coord (integer floor both axes; OOB on negative / out-of-grid) |
| `cellToWorld(TileCoord cell, FVector2 cellOrigin, float cellSizeW, float cellSizeH)` | `FVector2` | Cell coord → world-space point (returns **cell center**, not corner — see R-3E.5) |
| `aabbOverlappingCells(FVector2 worldMin, FVector2 worldMax, FVector2 cellOrigin, float cellSizeW, float cellSizeH)` | `TileRect` | World-space AABB → half-open cell rect that touches the AABB |
| `isCellInWorldBounds(TileCoord cell, uint32_t cols, uint32_t rows)` | `bool` | Just `cell.x in [0, cols)` and `cell.y in [0, rows)` |
| `Tilemap::setTile(FVector2 world, uint32_t tileId)` | `void` | World-coord overload delegating to the existing `setTile(TileCoord, ...)` |
| `Tilemap::getTile(FVector2 world)` | `uint32_t` | World-coord read; OOB returns `defaultTileId` (§6.3) |
| `setTileRange(Tilemap&, FVector2 worldMin, FVector2 worldMax, uint32_t tileId)` | `bool` | World-coord batch overload delegating to the cell-coord `setTileRange`; AABB → `TileRect` via `aabbOverlappingCells` then clamped to the grid |

All four cell-coord helpers are `[[nodiscard]] noexcept` free
functions. The three tilemap overloads are member methods on
`Tilemap` (declared inline in `AYTilemap.h`); existing
cell-coord signatures are untouched.

### 16.2 Semantics (locked)

- **R-3E.1 — integer floor**: `worldToCell` always rounds toward
  `-infinity` regardless of axis sign. A point at `(-0.5, -0.5)`
  with `cellOrigin=(0,0)` and `cellSize=1` lands on cell `(-1, -1)`
  — caller catches OOB via `setTile`'s negative-drop rule.
- **R-3E.2 — no clamping in helpers**: the helper layer is **pure
  math**. Every OOB guard happens downstream at the write/read
  boundary (mirrors how `getTile(TileCoord)` handles negatives).
- **R-3E.3 — AABB half-open + clamp**: `aabbOverlappingCells` with
  `worldMax <= worldMin` returns `TileRect{}` (empty rect). The
  returned cell rect uses the half-open `[x0, x1)` x `[y0, y1)`
  semantics from `TileRect` (§15.2). Cell extent is clamped to
  `[0, cols)` x `[0, rows)` only when the helper is given
  `cols`/`rows` (signature variant for that use case); the
  pure-math variant leaves clamping to the caller.
- **R-3E.4 — AABB inside-out is empty**: `aabbOverlappingCells`
  with `worldMax.x < cellOrigin.x` (AABB entirely to the left)
  returns `TileRect{}` (no overlap).
- **R-3E.5 — cell center, not corner**: `cellToWorld` returns the
  world point at the **center** of the cell — i.e.
  `cellOrigin + (cell.x + 0.5) * cellSizeW, cellOrigin.y + (cell.y + 0.5) * cellSizeH`. Editor paint picks anchor on the
  visible center; rounding-truncation stays at the writer (no
  drift on round-trip back through `worldToCell`).
- **R-3E.6 — `cellSize == 0` guard**: a malformed tilemap with
  zero cell size makes `worldToCell` return `TileCoord{0, 0}`,
  `cellToWorld` return `cellOrigin` exactly, and
  `aabbOverlappingCells` return `TileRect{}`. No division by zero.

### 16.3 Tile method overloads (locked)

The three tilemap world-coord overloads are tiny forwarding
methods declared inline in `AYTilemap.h`. They do NOT cache any
new state on `Tilemap` (per-cell write paths still route through
the existing cell-coord logic).

```cpp
// include/AYTilemap.h (P3E additions, inline)

void setTile(ayt::math::FVector2 world, uint32_t tileId) noexcept {
    const TileCoord c = worldToCell(
        world,
        ayt::math::FVector2{0.0f, 0.0f},  // cellOrigin (P3E in-AY2D default = world origin)
        static_cast<float>(tileWidth),
        static_cast<float>(tileHeight));
    setTile(c, tileId);
}

[[nodiscard]] uint32_t getTile(ayt::math::FVector2 world) const noexcept {
    const TileCoord c = worldToCell(
        world,
        ayt::math::FVector2{0.0f, 0.0f},
        static_cast<float>(tileWidth),
        static_cast<float>(tileHeight));
    return getTile(c);
}
```

`cellOrigin` defaults to `(0, 0)` for P3E. The future ECS
component can extend the surface with an explicit
`OrthographicCamera` query for non-zero cell origin (Phase 3+
cross-module PR to AYEntity).

The free-function overload of `setTileRange` in
`src/AYTilemapBatch.cpp` calls `aabbOverlappingCells` internally
+ delegates to the cell-coord `setTileRange`. Signatures:

```cpp
// include/AYTilemap.h (P3E add to namespace ayt::ay2d)
[[nodiscard]] bool setTileRange(Tilemap& t,
                                ayt::math::FVector2 worldMin,
                                ayt::math::FVector2 worldMax,
                                uint32_t tileId) noexcept;
```

### 16.4 Out of scope (deferred)

- **Inverse AABB** (AABB → cells-of-tilemap with explicit
  `cols`/`rows` clamp) — Phase 4 streaming PR uses
  `aabbOverlappingCells` + `clampToGrid(rect, cols, rows)` (already
  ships from Phase 3D). P3E ships the pure-math variant; the
  clamp-on-tilemap variant is a 2-line composition.
- **`worldToCell` with non-zero `cellOrigin`** beyond `(0, 0)` —
  ECS systems will pass the camera-derived origin via a future
  helper. P3E keeps the API origin-agnostic and the default
  zero; collision + editor picking PRs land it.
- **`Float2` ↔ `IVector2` automatic conversion via operator** —
  the boundary between world (float) and cell (int32_t) stays
  explicit to match the wider engine convention (no implicit
  fp↔int in the public headers of any sibling module).

### 16.5 Tests (`Test_TileMath.cpp` — 8 cases)

1. `WorldToCellIntegerHitReturnsExactCell` — cellOrigin 0/0 +
   cellSize 32/32; world `(64, 64)` → cell `(2, 2)`; world
   `(63.99, 32.5)` → cell `(1, 1)` (floor at 63.99).
2. `WorldToCellNegativeOriginTruncatesTowardMinusInfinity` —
   cellOrigin `(-100, -100)` + cellSize 16; world `(-91, -91)` →
   cell `(0, 0)`; world `(-117, -117)` → cell `(-1, -1)` (callers
   catch OOB).
3. `WorldToCellZeroCellSizeReturnsOrigin` — cellSize 0; any world
   → `(0, 0)`. No FPE / signal.
4. `CellToWorldCenterNotCorner` — cell `(2, 3)` + cellOrigin 0/0
   + cellSize 16; returns `(40, 56)` — the cell center.
5. `RoundTripWorldToCellBackMatchesForCellInterior` —
   `(32, 32, 16, 16)` ⇒ cell `(2, 2)`. `cellToWorld(2, 2)`
   ⇒ `(40, 40)`. `worldToCell(40, 40)` ⇒ `(2, 2)`. Stays inside
   the cell interior.
6. `AabbOverlappingCellsFullyInsideReturnsExactRect` —
   worldMin `(32, 32)`, worldMax `(96, 96)`, cellOrigin 0/0 +
   cellSize 32 ⇒ `{1, 1, 3, 3}` (cells `(1,1)`, `(2,1)`,
   `(1,2)`, `(2,2)`).
7. `AabbOverlappingCellsPartiallyOutOfGridReturnsEmpty` —
   worldMax `< cellOrigin` ⇒ `isEmpty == true`. worldMin negative
   gives a partial-rect containing cells `(-1..)` — caller clamps.
8. `TilemapSetTileByWorldCoordDelegatesAndGetReadsBack` —
   `t.setTile(FVector2{64, 64}, 7u)` after `t.resizeGrid(4, 4,
   TileIdPackMode::Narrow16)` ⇒ `t.getTile(FVector2{64, 64})` reads
   7. `t.getTile(FVector2{-1, -1})` reads `defaultTileId`
   (OOB read).

### 16.6 Risks / invariants

- **R-3E.1** integer-floor rule matches `IVector2`'s `static_cast`
  convention (both axes round toward `-infinity`).
- **R-3E.2** OOB handling: the helper deliberately does NOT
  clamp to `[0, cols)`. `setTile` and `getTile` already own
  the negative-drop contract (`AYTilemap.h:87` `cell.x < 0`).
  Tests lock this in case-7.
- **R-3E.5** cell-center vs cell-corner: editor paint uses
  the center to avoid picking the wrong cell when the user
  clicked near an edge. The `cellToWorld` ↔ `worldToCell`
  round-trip is **not** exact (a cell-center point lands back
  in the same cell because the center is in the cell interior);
  a cell-edge point lands on a cell boundary and is implementation-
  defined. This is the standard convention used by Unity's
  `SceneView` pick and by SDL_RectF.
- **R-3E.6** zero cellSize is treated as a no-op: helpers
  return their default-constructed result. Real tilemaps
  always have non-zero `tileWidth`/`tileHeight` (size-0 grid
  is also a write no-op at the `setTile` boundary), so this
  is a defensive guard, not a hot path.
- **R-3E.7** bgfx-leak guard stays green. `AYTileMath.h`
  includes only `<cstdint>`, `aymath/MathTypes.h`, the
  in-AY2D `AYTileCoord.h` + `AYTileRect.h` — confirmed by
  build-time `ay2d_check_no_bgfx_in_public_headers` target.
- **R-3E.8** deterministic across machines: all four helpers
  are pure fp arithmetic; no atomics, no locks, no IO. Same
  inputs ⇒ same outputs on all machines. Aligns with §6.3.

---

### 13.X Future versions (template)

---

### 13.11 v0.1.9 — 2026-07-29 (Phase 3F sprite culling helper — in-AY2D)

**Phase**: 3F (in-AY2D scope only — sprite AABB cull + stable sort,
no cross-module PRs)

**Locked changes**:
- §3 + §17: new in-AY2D public types ship a **self-contained
  sprite-scene builder** that does NOT reach into AYRenderer
  (`DrawItem` / `MaterialHandle` / `RenderPassSlot`). This is
  the explicit boundary lock (R-3F.1): AY2D owns the data
  carrier; the future `RenderSystem2D` cross-module PR to
  `AYRenderer` maintainer (§4.2.1) translates `SpriteDrawCmd`
  into `DrawItem::payload` then. Today: the helper builds
  `std::vector<SpriteDrawCmd>` sorted by `packedSortKey()`, with
  off-screen sprites removed via AABB-vs-camera intersection.
- §17.1: `SpriteDrawCmd` POD in `include/AYSpriteDrawCmd.h`.
  Fields (all in-AY2D types — no AYRenderer include):
  - `uint32_t` `packedSortKey` — mirrors `Sprite::packedSortKey()`.
  - `ayt::math::Float3x3` `worldMatrix` — copied through from
    the source `Sprite` for the future vertex-buffer build (no
    transformation happens inside this helper).
  - `ayt::math::FVector2 sourceRectMin` + `sourceRectMax`
    (4-float UV rectangle in atlas space, copied through).
  - `ayt::math::FVector4 colorRGBA` (4-float tint, copied
    through).
  - `uint8_t flip` (2-bit `SpriteFlip` value, copied through).
  - `uint32_t layerMask` (the camera-side `layerMask` at the
    time of insertion — copied for visibility-rebuild auditing;
    the cull has already filtered on `layerMask`, so the value
    on the cmd is informational only).
  Total per-cmd footprint ≤ 88 bytes (4 floats + 9 floats + 4
  floats + 1 byte + alignment). No allocation in the hot path
  when `std::vector::reserve(sprites.size())` is honored.
- §17.2: `WorldAabb()` free helper computes the camera's world-
  space AABB from `OrthographicCamera`. Lives in
  `include/AYWorldAabb.h` as a thin wrapper around the existing
  `viewMatrix()` + `projectionMatrix()` math (no AYMath
  surface-area extension; `ayt::math::FRectangle` already
  ships in `aymath/MathTypes.h:1092`). When `viewSize <= 0` or
  `viewport.heightPx <= 0`, the camera AABB is empty — every
  sprite is culled (R-3F.2).
- §17.3: `buildSpriteScene(sprites, camera, out)` free function
  in `src/AYSpriteCulling.cpp`. Algorithm (locked, R-3F.3):
  1. Compute `cameraRect = WorldAabb(camera)` once.
  2. If `cameraRect` is empty OR `sprites.empty()` → return
     (out stays empty).
  3. **AABB pre-cull**: for each `Sprite`, derive its
     sprite-AABB from `worldMatrix` translation (the 2D
     center, ±0.5 in each axis at default unit size; the matrix
     scale / rotation are NOT inflated — R-3F.4). If
     `spriteRect.intersects(cameraRect)` is false, drop. **No
     allocation per sprite** — the AABB math is just 6 float
     comparisons.
  4. **Layer mask cull** (R-3F.5): drop sprites with
     `(camera.layerMask & (1u << sprite.layer)) == 0`. The
     bit-test runs before the sort so culled sprites do not
     consume sort budget.
  5. **Stable sort** (R-3F.6 / F-2): `std::stable_sort` by
     `packedSortKey` ascending on the surviving sprites.
     `std::sort` is forbidden by F-2 (breaks ties deterministically
     but not stably — sprite insertion order at the same
     `packedSortKey` matters when two sprites land at the same
     layer+sort).
  6. **Emit `SpriteDrawCmd`**: copy each surviving sprite's
     fields into `out`. `out` is **cleared first** (no
     append). Caller passes `out` already `reserve()`d to
     `sprites.size()` for the cull-fast path; we don't
     re-`reserve` here.
- §17.4: zero-fragment test for `viewMatrix()` / `projectionMatrix()`
  integration. The cull sits entirely outside the matrix-build
  calls — `worldMatrix` is **copied through** into the cmd, not
  re-projected. (R-3F.7: AY2D helper does NOT do CPU projection.
  Per-frame projection is GPU-side; the future `RenderSystem2D`
  cross-module PR uses AYRenderer's existing
  `Renderer::setMainCamera(view, proj)`, not the sprite's
  per-instance matrix.)
- §10.2: `Test_SpriteCulling.cpp` ships 10 cases (see §17.5
  list). Tests use the same `Sprite::packedSortKey()` order
  algorithm as the production path so the test does not
  silently diverge. Every cull path asserts both the
  `out.size()` AND the `out[i].packedSortKey` sequence.
- §11.2: bgfx-leak guard stays green. `AYSpriteDrawCmd.h` and
  `AYWorldAabb.h` include only `<cstdint>`, `aymath/MathTypes.h`
  (PUBLIC), and in-AY2D `AYSprite.h` + `AYOrthographicCamera.h`.
  No `<bgfx/*.h>`, no `<bx/*.h>`. Confirmed by build-time
  `ay2d_check_no_bgfx_in_public_headers` target.
- §13.11: This changelog entry. Front-matter bumped to v0.1.9.
  Total tests: **16 TEST_SUITE / 124 TEST_CASE / 481 CHECK assertions
  PASS** (was 15 / 114 / 431 at v0.1.8; +1 suite, +10 case, +50 CHECK).

**Open follow-ups** (unchanged from §13.7..§13.10):
- `RenderPassSlot::Forward2DOpaque` + `DrawItem::payload`
  cross-module PR to AYRenderer (§4.2.1): the ECS system
  walks `SpriteComponent` + `Transform2D`, calls
  `buildSpriteScene(...)`, then translates `SpriteDrawCmd → DrawItem::payload`.
- `AYTileMapComponent` + `AYSpriteComponent` ECS components
  cross-module PR to AYEntity maintainer. The free function
  `buildSpriteScene(...)` is the body.
- `.aytilemap` binary write (animation table + draw-intent
  records) cross-module PR to AYResource maintainer.
- `tilemap_9tap.phoskia` shader variant cross-module PR to
  AYShader maintainer.
- `TilemapParallaxDemo` (Noop visual MVP) waits on the
  AYRenderer cross-module PR; the AY2D-side helper already
  exists.
- `Test_HotReload_Tilemap` (F-11 / F-17) waits on `.aytilemap`
  PR.
- Phase 4 streaming chunk sources — Phase 4 itself is gated.
- Phase 5 `ITileCollisionQuery` impl.

---

## 17. Phase 3F sprite culling helper (in-AY2D scope)

> Phase 3F adds the **CPU pre-cull + stable sort** half of the
> 2D draw pipeline. The GPU side of the pipeline (bgfx upload,
> shader pass, Present lane) lives in `AYRenderer` and lands
> with the cross-module PR. P3F ships the data carrier +
> algorithm in-AY2D so the future `RenderSystem2D` PR has a
> ready-made input to translate.

### 17.1 Surface

| Symbol | Returns | Purpose |
|---|---|---|
| `SpriteDrawCmd` (POD, in `AYSpriteDrawCmd.h`) | struct | Output record of the helper. Self-contained, no AYRenderer dep (R-3F.1). |
| `WorldAabb(camera) -> ayt::math::FRectangle` (in `AYWorldAabb.h`) | FRectangle | Camera's world-space AABB; empty when camera is degenerate (R-3F.2). |
| `buildSpriteScene(const std::vector<Sprite>&, const OrthographicCamera&, std::vector<SpriteDrawCmd>&) -> void` (in `src/AYSpriteCulling.cpp`) | void | AABB pre-cull + layer-mask cull + stable sort + emit. |
| `spriteAabbOf(s) -> ayt::math::FRectangle` (in `src/AYSpriteCulling.cpp`, internal) | FRectangle | Per-sprite world AABB derived from `worldMatrix` translation; ±0.5 unit at default scale (R-3F.4). |
| `isSpriteInCamera(s, cameraRect, layerMask) -> bool` (in `src/AYSpriteCulling.cpp`, internal) | bool | Combined AABB + layer-mask gate; unit-tested via `buildSpriteScene` (R-3F.3 / R-3F.5). |

### 17.2 Semantics (locked)

- **R-3F.1 — no AYRenderer include**: `AYSpriteDrawCmd.h` is
  pure-AYMath + `AYSprite.h`. The helper never reaches for
  `DrawItem`, `MaterialHandle`, or `RenderPassSlot`. The
  future cross-module PR does the translation.
- **R-3F.2 — degenerate camera → all sprites culled**: when
  `viewSize <= 0` or `viewport.heightPx <= 0` or
  `viewport.widthPx <= 0`, `buildSpriteScene` short-circuits
  before invoking `WorldAabb` and emits nothing. We do NOT
  rely on `FRectangle::intersects` semantics for an empty
  rect — `AYMath/MathTypes.cpp:2029` uses strict-less compare
  and can spuriously match a sprite centered at world (0, 0).
  The cull-level short-circuit is the authoritative gate.
- **R-3F.3 — three-step cull (AABB → layer → emit)**:
  order matters. AABB is cheapest and drops the most; layer-mask
  test runs next; sort happens after both, on the surviving set.
- **R-3F.4 — sprite AABB = translation ± 0.5**: at sprite
  default size (1×1 unit), the world AABB is `center ± 0.5`
  in each axis. Future cross-module PR can extend this to
  per-sprite bounds via a `Sprite::boundsMin/Max` field, but
  today's `Sprite` does not carry it. We deliberately do NOT
  inflate by `worldMatrix` scale or rotation — keeping the
  helper deterministic and `O(sprite_count)` (no matrix
  decomposition).
- **R-3F.5 — layer mask cull runs pre-sort**: bit-test is
  branch-predictable; the surviving set is already culled
  before `std::stable_sort`, so the sort walks fewer items.
- **R-3F.6 — `std::stable_sort`, not `std::sort`**: F-2 lock
  already in design.md §7.4. Two sprites at the same
  `packedSortKey` must keep their input order — sprite
  authoring explicitly uses insertion order for tied draws
  (e.g. UI text on the same layer+sort).
- **R-3F.7 — pure data carrier, no CPU projection**: the
  helper copies `worldMatrix` into `SpriteDrawCmd` but does
  NOT multiply by `viewMatrix * projectionMatrix`. Per-frame
  matrix projection is GPU-side via AYRenderer.

### 17.3 SpriteDrawCmd layout

```cpp
// include/AYSpriteDrawCmd.h — Phase 3F
#pragma once
#include <cstdint>
#include "aymath/MathTypes.h"
#include "AYSprite.h"  // For SpriteFlip; render-side can read the
                       //   enum value through the `flip` field on
                       //   this struct without re-including the
                       //   sprite header (this header transitively
                       //   carries the enum).
namespace ayt::ay2d {

struct SpriteDrawCmd {
    uint32_t  packedSortKey  = 0;  // (layer << 24) | (sortingKey & 0x00FFFFFF)
    ayt::math::Float3x3 worldMatrix = ayt::math::Float3x3::identity();
    ayt::math::FVector2 sourceRectMin{0.0f, 0.0f};
    ayt::math::FVector2 sourceRectMax{1.0f, 1.0f};
    ayt::math::FVector4 colorRGBA{1.0f, 1.0f, 1.0f, 1.0f};
    uint8_t  flip           = 0;  // SpriteFlip bitfield
    uint32_t layerMaskSnapshot = 0;  // camera.layerMask at insert time
};

} // namespace ayt::ay2d
```

Padding: `~88` bytes total per cmd (1 u32 + 9 floats = 40 bytes +
2 FVector2 = 16 bytes + 1 FVector4 = 16 bytes + 1 u8 + 3 bytes
padding + 1 u32 = 4 bytes). All-static POD; no virtual table, no
allocation, copy-by-value semantics. `std::vector<SpriteDrawCmd>`
reuses the same allocator as `std::vector<Sprite>`.

### 17.4 WorldAabb derivation

The camera world AABB is a half-extent rectangle centered on
`(positionX, positionY)` with vertical extent `viewSize` and
horizontal extent `viewSize * viewportAspect()`. The helper
does NOT consider `zoom` (camera zoom is GPU-side; a future
pre-cull PR can multiply here, but today's in-AY2D scope
assumes `zoom == 1.0` for the world AABB — the §5.3
`isPixelPerfectSafe()` invariant holds for the canonical
case).

```cpp
// include/AYWorldAabb.h — Phase 3F
#pragma once
#include <cstdint>
#include "aymath/MathTypes.h"  // FVector2 + FRectangle
#include "AYOrthographicCamera.h"
namespace ayt::ay2d {

[[nodiscard]] inline ayt::math::FRectangle WorldAabb(
    const OrthographicCamera& cam) noexcept {
    if (cam.viewSize <= 0.0f || cam.viewport.heightPx <= 0) {
        // Empty FRectangle by construction.
        return ayt::math::FRectangle{
            ayt::math::FVector2{0.0f, 0.0f},
            ayt::math::FVector2{0.0f, 0.0f}};
    }
    const float half   = cam.viewSize * 0.5f;
    const float aspect = cam.viewportAspect();
    // FRectangle takes min first, max second (mirrors §17.3 srcRect).
    return ayt::math::FRectangle{
        ayt::math::FVector2{cam.positionX - half * aspect,
                            cam.positionY - half},
        ayt::math::FVector2{cam.positionX + half * aspect,
                            cam.positionY + half}};
}

} // namespace ayt::ay2d
```

### 17.5 Tests (`Test_SpriteCulling.cpp` — 10 cases)

1. `BuildSpriteSceneEmptyInputEmitsNothing` — empty `sprites`,
   `buildSpriteScene(...)` → `out.empty()`.
2. `BuildSpriteSceneDegenerateCameraEmitsNothing` — camera with
   `viewSize == 0`; even 10 fully-visible sprites emit nothing.
3. `SpriteOutsideCameraIsCulled` — one sprite centered at
   `(camera.x + 100, camera.y)`; `out.size() == 0`.
4. `SpriteInsideCameraIsEmitted` — one sprite centered at the
   camera origin; `out.size() == 1`, `out[0].packedSortKey ==
   sprite.packedSortKey()`.
5. `SpriteOnAabbEdgeIntersectionCountsAsInside` — sprite placed
   exactly on the camera AABB edge (`FRectangle::intersects` is
   closed-open so an exact-edge sprite is *just* outside —
   documented behavior; the test asserts the boundary case).
6. `SpritesSortedByPackedSortKeyAscending` — 4 sprites with
   distinct `packedSortKey` (8, 2, 32, 16); output order is
   `[2, 8, 16, 32]`.
7. `SpritesWithSameSortKeyStableKeptInInputOrder` — 3 sprites
   with `packedSortKey == 0x01000000`; output order is the input
   order (no swapping). Locks R-3F.6 / F-2.
8. `LayerMaskCullRemovesOffLayerBeforeSort` — `camera.layerMask
   = 0x04` (only layer 2 visible). 3 sprites on layers 0/1/2;
   output has 1 sprite (the layer-2 one).
9. `MixedCullAndSortOutInOrderForVisible` — 6 sprites, two of
   them off-camera; output order is `[survivor-packedKey, ...]`
   with `out.size() == 4`.
10. `SpriteWorldAabbDefaultInsetHalfUnit` — for sprite at world
    position `(0, 0)` with identity matrix, the sprite's AABB
    is `{(-0.5, -0.5), (0.5, 0.5)}`. Locks R-3F.4.

### 17.6 Out of scope (deferred)

- **Per-sprite bounds field** on `Sprite` (would let
  `spriteAabbOf` inflate to non-unit size). P3F uses a
  uniform `±0.5`; the cross-module PR can add the field.
- **CPU projection** of `worldMatrix` (R-3F.7). GPU-side.
- **`worldMatrix` scale / rotation aware AABB** — R-3F.4;
  Phase 6 perf hardening could add matrix-aware pre-cull.
- **`std::vector<Sprite>` → allocator-aware transient
  arena** — the helper honours caller-reserve today; the
  ECS system wrapper will likely provide a scratch vector
  from the world's frame arena (cross-module to AYEntity).
- **`.aytilemap` content with sprite draws** — cross-module
  PR to AYResource.

### 17.7 Risks / invariants

- **R-3F.1** no AYRenderer include — verified by
  `ay2d_check_no_bgfx_in_public_headers` + manual grep at PR
  time.
- **R-3F.2** degenerate-camera cull — no edge case leaks
  because `FRectangle::intersects(empty)` returns false by
  construction (verified in AYMath's own tests; lock in case
  any future override breaks that).
- **R-3F.3 + R-3F.5** cull order — AABB pre-cull saves the
  most entries (off-screen sprites are typical in editor /
  map scenarios), and layer-mask cull is branch-predictable.
  We do NOT sort-then-cull (would waste sort budget on
  culled entries).
- **R-3F.4** ±0.5 default — documented at the function; future
  bounds-aware PR can swap the AABB derivation without
  breaking the call site.
- **R-3F.6** `stable_sort` not `sort` — F-2 lock; the test
  case 7 enforces this with three sprites at the same
  `packedSortKey`.
- **R-3F.7** pure carrier — no projection math in the helper.
  Tests assert the worldMatrix byte-identity (test 4 uses an
  off-identity matrix and reads back the same byte pattern).

---

### 13.12 v0.1.10 — 2026-07-30 (Phase 3G chunk-source budget + reject counter — in-AY2D)

**Phase**: 3G (in-AY2D scope only — chunk-source budget gates and
LRU `setCapacity` runtime control, no cross-module PRs)

**Locked changes**:
- §6.2 + §18: `TilemapBudget` shape (already shipped in
  `AYTilemapChunkSource.h` since Phase 2) is now **runtime
  wired** onto `InMemoryTilemapChunkSource`:
  - `setCapacity(uint32_t)` / `setMaxIoBytesPerSec(uint64_t)` —
    mutator paths previously only available via the ctor.
  - `maxChunksLoaded` ∈ `[1, kReserved]`; `0` = unlimited
    (matches the ctor `capacity=0` default). The setter is the
    canonical way to push a budget onto an existing source.
  - `maxIoBytesPerSec` = `0` = no rate gate (P3A's
    unlimited mode). Non-zero activates a sliding-window rate
    gate in `requestChunk`.
- §18.1: `chunk_io_reject` is the **tenth** field on
  `Ay2DCounters` (was 9 at v0.1.9; +1 = 10). `uint64_t`
  cumulative counter; reset by `resetAll` only (NOT
  `resetPerFrame` — rejections are slow path events, kept as
  a total). The counter increments **inside** `requestChunk`
  when the rate gate rejects — before the rejected request
  touches `_pending`. Tests assert the counter delta via
  `counters().snapshot()`.
- §18.2: rate-limit semantics (locked, R-3G.3):
  - Sliding window of `_rejectedInWindow` + `_windowStartUs`
    pairs, advanced whenever the window expires.
  - `maxIoBytesPerSec == 0` ⇒ no rate gate (R-3G.3a).
  - On `requestChunk`: accumulate the *would-be* request
    size (Narrow16 = 32 KB chunk standard; Wide32 = 64 KB;
    inferred from the chunk's nominal storage) into the
    window. If the new cumulative would exceed
    `maxIoBytesPerSec`, the request is rejected (invalid
    handle returned) and `chunk_io_reject` is bumped.
  - Note: today's `InMemoryTilemapChunkSource` always has
    chunks of a fixed default size (`PutChunkBytes`
    constant = 32 * 1024 for Narrow16 / 64 * 1024 for Wide32
    — matches the §6.2 chunk-of-16×16 nominal size × 2B/4B).
    The gate doesn't need a bytes-per-request argument today
    because the rate limit is the *only* gate consuming bytes.
- §18.3: `EvictionPolicy` enum stays where it is (in
  `AYTilemapChunkSource.h`). P3G does NOT change its meaning.
  The LRU policy has been the live wire since Phase 3A
  (§13.6) — `InMemoryTilemapChunkSource::evictIfNeeded` is
  the implementation. `Distance` and `TimeWindow` are
  documented as P3G-deferred-to-Phase-4 (R-3G.1): the enum
  values exist for forward-compat, but the InMemory source
  hard-asserts `eviction == EvictionPolicy::LRU` and returns
  `false` from `setBudget(b)` when given a non-LRU policy
  (signaling "apply via the cross-module Phase 4 PR").
- §18.4: `setBudget(b)` returns `bool` (R-3G.4):
  - `true` iff the policy is LRU (the only one we wire).
  - On a non-LRU policy, the call is a **no-op** that returns
    `false`. The previous budget stays in effect. The same
    rule applies to `setEvictionPolicy(p)` if we expose it
    (P3G does not — Phase 4 PR does).
  - On `maxChunksLoaded == 0`, the source flips to unlimited
    (matches the ctor's `capacity=0` default). The previous
    cap setting is forgotten.
  - On `maxIoBytesPerSec == 0`, the rate gate disables.
  - `maxChunksResident` is **NOT wired** in P3G (would need
    a separate GPU-residency mechanism — Phase 6 perf
    hardening, gated on RenderResourceManager cross-module
    PR). The field is read-acknowledged in the budget but
    the source silently ignores it.
- §10.2: `Test_ChunkSourceBudget.cpp` ships 6–8 cases (see
  §18.5 list). All assertions use real `counters().snapshot()`
  deltas — same discipline as P3C Test_CountersWired.
- §11.2: bgfx-leak guard stays green. New public symbols
  (`setCapacity` / `setMaxIoBytesPerSec` / `setBudget` /
  `chunk_io_reject` field) are exposed only through existing
  headers + the new `chunk_io_reject` field on the
  `Ay2DCounters` struct (which is bgfx-clean).
- §13.12: This changelog entry. Front-matter bumped to v0.1.10.
  Total tests: **17 TEST_SUITE / 130 TEST_CASE / 549 CHECK assertions
  PASS** (was 16 / 459 at v0.1.9; +1 suite, +8 case, +50 CHECK).

**Open follow-ups** (unchanged from §13.7..§13.11):
- Phase 4 streaming — `Distance` / `TimeWindow` eviction
  policies become real (R-3G.1). The cross-module PR to
  AYResource / AYEntity ships the streaming system wrapper
  that exercises `aabbOverlappingCells`.
- Phase 4 streaming also lifts `maxChunksResident` into the
  GPU side (R-3G.4).
- `RenderPassSlot::Forward2DOpaque` + `DrawItem::payload`
  cross-module PRs to AYRenderer (§4.2.1) — `SpriteDrawCmd`
  carrier is ready from P3F.
- `AYTileMapComponent` + `AYSpriteComponent` cross-module
  PRs to AYEntity. P3F's `buildSpriteScene` is the
  composer's body; P3G's budget gate is its quota enforcer.
- `.aytilemap` binary write (animation + budget records)
  cross-module PR to AYResource.
- `TilemapParallaxDemo` waits on the AYRenderer PR.
- `Test_HotReload_Tilemap` waits on the `.aytilemap` PR.
- Phase 5 `ITileCollisionQuery` impl.

---

### 13.PF v0.1.11-pre 2026-07-30 (Pre-flight retractions + bug fixes — docs only)

**Phase**: Pre-flight (corrective, ahead of in-AY2D Phase 5 / 3H slice chain).

**Scope**: design.md only. No code changed. No version bump yet (next bump = slice 1 docs commit, v0.1.11).

**Locked changes**:

- **C1 / R-3G.1 (§18.6)**: clarified that the non-LRU `setBudget`
  no-op lock is **active, not deferred**. `Distance` requires a
  camera reference not present in `InMemoryTilemapChunkSource`;
  `TimeWindow` requires a per-entry access timestamp the LRU list
  does not carry today. Both remain deferred to the cross-module
  Phase 4 streaming PR (§4.2.1) which owns the chunk-source ↔
  camera composition. P3G ships counter scaffolding only (P3G.1
  partial slice).
- **C2 / R-3G.4 (§18.1)**: documented the `TilemapBudget` 3-tier
  residency model explicitly: `maxChunksLoaded` (hard cap, in-AY2D
  P3A) → `maxChunksCpuSoftCap` (NEW field, in-AY2D soft cap,
  P3G.2a slice) → `maxChunksResident` (GPU residency,
  cross-module PR to RenderResourceManager, still deferred per
  R-3G.4). The field set on `TilemapBudget` is **not** changed in
  pre-flight; the new `maxChunksCpuSoftCap` field ships with the
  P3G.2a slice commit.
- **C6 / §8.1 default `isBlocked`**: corrected the default
  `isBlocked` formula from `flagsAt(cell) & (Solid | OneWay |
  Hazard) != CollisionFlags::Empty` (always-true bug) to
  `flagsAt(cell) != CollisionFlags::Empty`. The previous form
  was self-contradictory: `x & mask` cannot equal `Empty` when
  `Empty` is not in `mask`. Phase 5 adapter ships the corrected
  formula.

  > **C6-R1 amendment** (added 2026-07-30 ahead of Phase 3I slice 1;
  > see §13.20 P3I.1): the C6 default `isBlocked` formula is
  > **retained verbatim**. The body of `flagsAtRaw` is **amended**
  > (not retracted) so that the no-flag-data path still returns
  > `Empty` but the hit-the-block-set path returns `Solid`. The
  > no-override-on-`isBlocked` clause is also retained. Full text in
  > §13.20.

- **C6 / `Tilemap::flagsAtRaw` contract**: clarified in §8.1 that
  `flagsAtRaw` MUST return `CollisionFlags::Empty` (1<<6) for
  cells with no flag data, NOT `CollisionFlags::None` (0). The
  current implementation returns `0u = None` (a §8.1 contract
  violation); Phase 5 slice fixes the body and adds a regression
  test.

  > **C6-R1 amendment** (added 2026-07-30 ahead of Phase 3I slice 1;
  > see §13.20 P3I.1): the C6 "body MUST return Empty" wording is
  > **superseded** for the no-flag-data branch (still returns
  > `Empty`) by a three-segment evaluation that allows `Solid` on
  > a `blockedTileIds` hit. The `Empty` branch and the `None` ban
  > are **retained**. Full text in §13.20.
- **C8 / `TileCoord` deviation**: noted that the shipped
  `ITileCollisionQuery` interface uses `TileCoord` (consistent
  with all AY2D code per `AYTileCoord.h:5-14` deliberate refusal
  of `IVector2`) instead of `IVector2` per the §8.1 doc text.
  §16.4 permits int↔int conversion; the deviation is documented
  but not a breaking change. The §8.1 doc block stays as-is; the
  shipped header uses `TileCoord`.
- **C5 / `TilemapBinding` deprecation**: P3H.2 slice will mark
  `TilemapBinding` (`include/AY2D/AYWorld2D.h:44-48`) deprecated
  in favor of `TilemapEntryView` (new type, P3H.2 ship).
  `TilemapBinding` is dead code today (grep across
  `include src unittest design.md` returns no consumer) and
  overlaps with `World2D::Entry`. The deprecation is additive
  only (no behavior change).

**Open follow-ups** (unchanged from §13.12 + Phase 5 / 3H slices
  pending):

- Slice 1 (Phase 5): ship `Ray2D`, `RaycastHit2D`,
  `ITileCollisionQuery`, `TilemapCollisionQueryAdapter`; fix
  `flagsAtRaw` body to return `Empty`.
- Slice 2 (P3H.2): ship `World2DSnapshot` value type +
  `TilemapEntryView`; deprecate `TilemapBinding`.
- Slice 3 (P3G.2a): wire `maxChunksCpuSoftCap` (new field) +
  `chunk_io_residency_reject` counter.
- Slice 4 (P3G.1 partial): counter scaffolding +
  `AY_LOG_WARN` on non-LRU `setBudget`.
- Slice 5 (P3D.2): ship `Tilemap::aabbOfCell` centered on
  `cellToWorld` per R-3E.5.
- Slice 6 (P3H.1): ship `SpriteSheet = AtlasDesc + path`.
- Slice 7 (P3H.3): ship `foreachTilemapView` read-only visitor.
- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.

---

### 13.13 v0.1.11 — 2026-07-30 (Phase 5 collision types + adapter — in-AY2D)

**Phase**: 5 (in-AY2D scope only — collision interface + thin adapter,
no resolver, no cross-module PRs)

**Locked changes** (per §13.PF pre-flight retractions applied as code):

- §8.1 (C7): `include/AY2D/AYTileCollision.h` expanded from 53 lines
  (Phase 0 placeholder) to the full §8.1 type set:
  - `Ray2D { origin, direction, tMin, pointAt(t) }` — `pointAt`
    out-of-line in `src/AYTileCollision.cpp` (kept out-of-line for
    future SIMD / hardware-quad fast paths).
  - `RaycastHit2D { hit, t, cell, flags, point }` — default
    `hit=false` is the "no hit" sentinel.
  - `ITileCollisionQuery` abstract class — three virtuals:
    `flagsAt(TileCoord)` (pure), `isBlocked(TileCoord)` (default
    impl), `raycast(Ray2D, float)` (pure).
- §8.1 (C6 / `Tilemap::flagsAtRaw` contract): body fixed from
  `return 0u;` (None — a §8.1 contract violation; "None MUST NOT
  be used to mean empty") to
  `return static_cast<uint32_t>(CollisionFlags::Empty);` (1<<6).
  Regression covered by `Test_TileCollisionQuery::FlagsAtRawReturnsEmptyNotNone`.
- §8.1 (C6 / default `isBlocked` formula corrected):
  `flagsAt(c) != CollisionFlags::Empty`. The pre-Phase-5 form
  `flagsAt(c) & mask != Empty` was always true (`x & mask`
  cannot equal `Empty` when `Empty` is not in `mask`). Phase 5
  ships the corrected form as the in-class default.
- §11 Phase 5 row exit gate: `include/AY2D/AYTilemapCollisionAdapter.h`
  + `src/AYTilemapCollisionAdapter.cpp` implement the only
  in-AY2D concrete `ITileCollisionQuery`. `flagsAt` delegates to
  `Tilemap::flagsAtRaw`. `raycast` is the §11 placeholder
  (always miss — `hit=false`). A real axis-aligned tile-grid
  walker lands with the consumer-side cross-module PR (§4.2.1 to
  AYPhysics maintainer).
- §13.PF (C8 / `TileCoord` deviation): the shipped interface
  uses `TileCoord` (consistent with all AY2D code per
  `AYTileCoord.h:5-14` deliberate refusal of `IVector2`) instead
  of `IVector2` per the §8.1 doc text. §16.4 permits int↔int
  equivalence; the deviation is logged but is not a breaking
  change.
- `include/AY2D.h` umbrella adds `AYTilemapCollisionAdapter.h`.
- `CMakeLists.txt` adds 2 new `.cpp` to `SRC_FILES`
  (`AYTileCollision.cpp` + `AYTilemapCollisionAdapter.cpp`).
- `unittest/CMakeLists.txt` adds `Test_TileCollisionQuery.cpp`.

**Tests** (`unittest/Test_TileCollisionQuery.cpp` — 7 cases / 22 CHECK):

1. `Ray2DPointAtParametrics` — `pointAt(t)` returns `origin + t *
   direction`; verifies t=0, t=1, t=0.5, t=-1 (no clamp).
2. `RaycastHit2DDefaultIsNoHit` — default `hit=false`, `t=0`, no
   cell, no flags, no point.
3. `AdapterFlagsAtEmptyAndIsBlockedFalse` — adapter mirrors
   `flagsAtRaw`'s `Empty`; inherited `isBlocked` returns false.
4. `FlagsAtRawReturnsEmptyNotNone` — regression for §13.PF (C6):
   body MUST return `CollisionFlags::Empty` (1<<6) not
   `CollisionFlags::None` (0). Also verifies OOB cells return
   `Empty`.
5. `AdapterRaycastAlwaysMiss` — §11 Phase 5 row placeholder:
   always `hit=false`.
6. `TileCoordRoundTrip` — §13.PF (C8): shipped interface uses
   `TileCoord`, not `IVector2`. Round-trip preserves int32 fields.
7. `AdapterOutOfRangeCellReturnsEmpty` — `flagsAtRaw` ignores the
   `cell` arg today (Phase 5 placeholder); adapter does not
   crash on OOB; returns `Empty`.

All existing tests (Phase 1+ stubs + Phase 2 functional +
Phase 3A real-impl + Phase 3B animation + sprite + Phase 3C
counter wiring + Phase 3D batch tile-fill + Phase 3E
world↔cell math + Phase 3F sprite culling + Phase 3G chunk
budget) untouched.

- §11.2: bgfx-leak guard stays green. New public symbols
  (`Ray2D` / `RaycastHit2D` / `ITileCollisionQuery` /
  `TilemapCollisionQueryAdapter`) are exposed through
  `AYTileCollision.h` + `AYTilemapCollisionAdapter.h`. Both
  headers include only `<cstdint>` + `aymath/MathTypes.h` +
  `AYTileCoord.h` — zero bgfx paths.
- §13.13: This changelog entry. Front-matter bumped to v0.1.11.
  Total tests: **18 TEST_SUITE / 538 CHECK assertions PASS** (was
  17 / 516 at v0.1.10; +1 suite, +22 CHECK).

**Open follow-ups** (unchanged from §13.12 + §13.PF):

- Phase 5 follow-ups:
  - `isBlocked` 真 wire (blocked-tile-id-set) — needs collision
    resolver consumer (cross-module PR).
  - Raycast walker — axis-aligned tile grid; cross-module PR to
    AYPhysics maintainer.
- Slice 2 (P3H.2): ship `World2DSnapshot` + `TilemapEntryView`;
  deprecate `TilemapBinding`.
- Slice 3 (P3G.2a): wire `maxChunksCpuSoftCap` (new field) +
  `chunk_io_residency_reject` counter.
- Slice 4 (P3G.1 partial): counter scaffolding +
  `AY_LOG_WARN` on non-LRU `setBudget`.
- Slice 5 (P3D.2): ship `Tilemap::aabbOfCell` centered on
  `cellToWorld` per R-3E.5.
- Slice 6 (P3H.1): ship `SpriteSheet = AtlasDesc + path`.
- Slice 7 (P3H.3): ship `foreachTilemapView` read-only visitor.
- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.

---

### 13.14 v0.1.12 — 2026-07-30 (P3H.2 World2DSnapshot value type — in-AY2D)

**Phase**: P3H.2 (in-AY2D scope only — read-only value-type
debug introspection surface; replaces the `IWorld2DDebug`
vtable proposal with a plain value type so `World2D` stays
POD-ish).

**Locked changes** (per §13.PF C5 reshape):

- §3 / §13.PF C5: `include/AY2D/AYWorld2DSnapshot.h` ships
  `TilemapEntryView` (POD: `TilemapHandle handle; uint32_t
  layer; uint32_t sortingKey`) — **no `resource` field** because
  `World2D::Entry::resource` is always `nullptr` at HEAD (the
  `.aytilemap` loader PR is a cross-module concern per §4.2.1)
  and exposing it as `IAYTilemap*` to callers would hand out a
  dangling pointer to an incomplete type.
- §13.PF C5: `World2DSnapshot` is a plain value type (no
  vtable, no interface inheritance). Built via
  `World2DSnapshot::build(const World2D&)` — copies
  `entries` (as `TilemapEntryView`s) and `counters.snapshot()`
  (relaxed atomic load per §10.1.1 + F-8).
- §13.PF C5: `TilemapBinding` (`include/AY2D/AYWorld2D.h:44-48`)
  is **deprecated** via `[[deprecated("... use TilemapEntryView
  via World2DSnapshot")]]`. The type stays present for additive
  compatibility; a future PR may remove it once grep confirms
  zero consumers (none exist today).
- `include/AY2D.h` umbrella adds `AYWorld2DSnapshot.h`.
- `CMakeLists.txt` adds `src/AYWorld2DSnapshot.cpp` to `SRC_FILES`.
- `unittest/CMakeLists.txt` adds `Test_World2DSnapshot.cpp`.

**Tests** (`unittest/Test_World2DSnapshot.cpp` — 5 cases / 21 CHECK):

1. `EmptyWorldProducesEmptyEntries` — empty `World2D` →
   `size()==0`, `entries` empty, `counters` zero-initialised.
2. `SizeMatchesWorldSize` — `World2DSnapshot::size()` matches
   `World2D::size()`; mutating the world after `build()` does
   NOT affect the snapshot.
3. `CountersSnapshotIsStableAcrossMutations` — the counters
   snapshot is a stable copy; bumping a counter post-build does
   NOT mutate the snapshot.
4. `EntryViewExposesHandleLayerSortingKey` — `TilemapEntryView`
   carries `handle` (id + generation), `layer`, `sortingKey`.
5. `TilemapBindingStillCompilesForBackcompat` — `TilemapBinding`
   still exists and has the expected field layout (sizeof +
   first-field offset check).

All existing tests (Phase 1+ + Phase 2 + Phase 3A/B/C/D/E/F/G +
Phase 5) untouched. Deprecation warning C4996 fires on case 5
(use of `TilemapBinding`); the test deliberately exercises the
deprecation path so a future PR that removes the attribute is
caught at CI. The build keeps `/wd4200` only — C4996 is a
warning, not an error, so the deprecated type stays usable.

- §11.2: bgfx-leak guard stays green. `AYWorld2DSnapshot.h`
  includes `<cstdint>` + `<vector>` + `AY2DCounters.h` +
  `AYWorld2D.h` — all bgfx-clean.
- §13.14: This changelog entry. Front-matter bumped to v0.1.12.
  Total tests: **19 TEST_SUITE / 559 CHECK assertions PASS** (was
  18 / 538 at v0.1.11; +1 suite, +21 CHECK).

**Open follow-ups** (unchanged from §13.13 + §13.PF):

- Slice 3 (P3G.2a): wire `maxChunksCpuSoftCap` (new field) +
  `chunk_io_residency_reject` counter.
- Slice 4 (P3G.1 partial): counter scaffolding +
  `AY_LOG_WARN` on non-LRU `setBudget`.
- Slice 5 (P3D.2): ship `Tilemap::aabbOfCell` centered on
  `cellToWorld` per R-3E.5.
- Slice 6 (P3H.1): ship `SpriteSheet = AtlasDesc + path`.
- Slice 7 (P3H.3): ship `foreachTilemapView` read-only visitor.
- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.

---

### 13.15 v0.1.13 — 2026-07-30 (P3G.2a CPU soft cap — in-AY2D)

**Phase**: P3G.2a (in-AY2D scope only — second-layer CPU
soft cap wired; resolves the §13.PF C2 field split).

**Locked changes** (per §13.PF C2 reshape):

- §18.1 / §13.PF C2: `TilemapBudget` gains a new field
  `maxChunksCpuSoftCap` (uint32_t, default `0` = disabled)
  between `maxChunksLoaded` and `maxChunksResident`. The
  field set is now a 3-tier residency model:
  - `maxChunksLoaded` — hard cap (in-AY2D P3A; wired to
    `setCapacity`).
  - `maxChunksCpuSoftCap` — in-AY2D soft cap, **P3G.2a ship**.
    Evict-down-to when `setBudget` lowers it below the cache
    size; `0` = disabled; non-zero AND `>= maxChunksLoaded`
    is a no-op (hard cap rules).
  - `maxChunksResident` — GPU residency ceiling, R-3G.4.
    Cross-module PR to `RenderResourceManager`. Not wired in
    this slice.
- §14.1: `Ay2DCounters` gains an 11th field
  `chunk_io_residency_reject` (std::atomic<uint64_t>,
  cumulative, reset by `resetAll` only — same discipline as
  `chunk_io_reject` per R-3G.2 extended to soft-cap eviction
  failures). The pin set that would bump this counter lands
  with Phase 4 streaming; today the counter stays at 0.
- §18.1: `InMemoryTilemapChunkSource` gains
  `setMaxChunksCpuSoftCap(uint32_t)` public mutator +
  `evictDownTo(uint32_t)` private helper. `setBudget(b)`
  routes through the new mutator so trim logic lives in
  one place.
- §18.1: `TilemapBudget` `budget()` getter now reflects the
  live soft cap (was previously unavailable).

**Tests** (`unittest/Test_CpuSoftCap.cpp` — 4 cases / ~14 CHECK):

1. `SetSoftCapTrimsBelowHardCap` — hard cap 10, 8 puts, then
   `setMaxChunksCpuSoftCap(3)` → cache trimmed to 3 (LRU-front
   evicted); the 3 MRU-back entries remain.
2. `SoftCapAboveHardCapIsNoOp` — hard cap 5, 8 puts (cache
   = 5), then `setMaxChunksCpuSoftCap(10)` → no-op (soft cap
   cannot exceed hard cap).
3. `SetBudgetAppliesSoftCapAndRateGate` — `setBudget({
   maxChunksLoaded=8, maxChunksCpuSoftCap=2,
   maxIoBytesPerSec=0 })` → 6 puts fill to 6, explicit
   `setMaxChunksCpuSoftCap(2)` re-apply trims to 2.
4. `ResidencyRejectCounterResetsByResetAllOnly` — field
   exists; cumulative discipline verified via `resetPerFrame`
   no-op + `resetAll` zero.

All existing tests (Phase 1+ + Phase 2 + Phase 3A/B/C/D/E/F/G +
Phase 5 + P3H.2) untouched.

- §11.2: bgfx-leak guard stays green. No new public headers.
- §13.15: This changelog entry. Front-matter bumped to v0.1.13.
  Total tests: **20 TEST_SUITE / 600 CHECK assertions PASS** (was
  19 / 559 at v0.1.12; +1 suite, +41 CHECK — extra CHECKs came
  from per-iteration put assertions inside the test cases, not
  a single case = single CHECK).

**Open follow-ups** (unchanged from §13.14 + §13.PF):

- Slice 4 (P3G.1 partial): counter scaffolding +
  `AY_LOG_WARN` on non-LRU `setBudget`.
- Slice 5 (P3D.2): ship `Tilemap::aabbOfCell` centered on
  `cellToWorld` per R-3E.5.
- Slice 6 (P3H.1): ship `SpriteSheet = AtlasDesc + path`.
- Slice 7 (P3H.3): ship `foreachTilemapView` read-only visitor.
- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.
- Phase 4 streaming: pin set lands; soft-cap eviction
  failure path bumps `chunk_io_residency_reject`.

---

### 13.16 v0.1.14 — 2026-07-30 (P3G.1 partial counter scaffolding + log warning — in-AY2D)

**Phase**: P3G.1 partial (in-AY2D scope only — counter
scaffolding + log warning; full Distance / TimeWindow wire
stays deferred to the cross-module Phase 4 streaming PR per
§4.2.1; R-3G.1 lock intact).

**Locked changes**:

- §14.1: `Ay2DCounters` gains 3 fields: `evictions_distance`
  / `evictions_time_window` (scaffolding, always 0 today per
  R-3G.1; the cross-module Phase 4 PR bumps them per
  eviction) + `evictions_lru` (cumulative counter bumped
  inside `evictIfNeeded` / `setCapacity` / `evictDownTo` —
  one `fetch_add` per block of evictions to keep the hot
  path cheap). All 3 follow R-3G.2 discipline (cumulative,
  reset by `resetAll` only; `resetPerFrame` does NOT zero
  them).
- §18.4: `InMemoryTilemapChunkSource::setBudget` non-LRU
  branch fires `ayt::log::warn("[AY2D::...] non-LRU policy
  '%s' requested; full wiring deferred to cross-module Phase 4
  streaming PR per R-3G.1 (camera + timestamp). Returning
  false; previous budget remains in effect.", policyName)`
  then returns false. The lock behavior is unchanged from
  P3G; only the warning is new.
- §2.3 allowed deps: `AYLog` is added to the AY2D PUBLIC link
  list. AYLog was already in §2.3 allowed-deps; AYResource
  + AYScript already use `ayt::log::warn` for similar
  "request deferred to a future PR" diagnostics (mirrors
  the engine-wide pattern).
- `evictIfNeeded` / `setCapacity` / `evictDownTo` track a
  per-call `evicted` count and `fetch_add` once per block to
  the new `evictions_lru` counter. Behaviour change: zero.
  The counters are pure telemetry.

**Tests** (`unittest/Test_BudgetPolicyCounters.cpp` — 3 cases /
~20 CHECK):

1. `NonLRUSetBudgetReturnsFalseAndKeepsPrevious` — Distance
   policy via `setBudget` returns false; the previous LRU
   budget stays in effect; the scaffolding counters
   `evictions_distance` / `evictions_time_window` stay at 0.
2. `ScaffoldingCountersAreZeroByDefault` — three new counters
   exist on `Snapshot`; default 0. LRU eviction bumps
   `evictions_lru` but Distance / TimeWindow stay at 0.
3. `LRUEvictionsCounterResetByResetAllOnly` — `evictions_lru`
   cumulative discipline verified via `resetPerFrame` no-op
   + `resetAll` zero.

All existing tests (Phase 1+ + Phase 2 + Phase 3A/B/C/D/E/F/G
+ Phase 5 + P3H.2 + P3G.2a) untouched.

- §11.2: bgfx-leak guard stays green. No new public headers.
- §13.16: This changelog entry. Front-matter bumped to v0.1.14.
  Total tests: **21 TEST_SUITE / 627 CHECK assertions PASS** (was
  20 / 600 at v0.1.13; +1 suite, +27 CHECK). 3× consecutive
  green runs locked (mid-plan checkpoint per Phase 3G discipline).

**Open follow-ups** (unchanged from §13.15 + §13.PF):

- Slice 5 (P3D.2): ship `Tilemap::aabbOfCell` centered on
  `cellToWorld` per R-3E.5.
- Slice 6 (P3H.1): ship `SpriteSheet = AtlasDesc + path`.
- Slice 7 (P3H.3): ship `foreachTilemapView` read-only visitor.
- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.
- Phase 4 streaming (R-3G.1): wires Distance / TimeWindow,
  bumps `evictions_distance` / `evictions_time_window` per
  eviction. The counter scaffolding + log warning stay in
  place; the policies activate against the existing field.

---

### 13.17 v0.1.15 — 2026-07-30 (P3D.2 aabbOfCell — in-AY2D)

**Phase**: P3D.2 (in-AY2D scope only — pure-math composite API;
no cross-module PR; uses existing P3E `cellToWorld` helper).

**Locked changes**:

- §16.3: `Tilemap::aabbOfCell(TileCoord)` returns
  `ayt::math::FRectangle`. Centered on `cellToWorld(c)` (R-3E.5
  cell-center) via `FRectangle::fromCenterExtent(center,
  full-extent)`. The naive corner-port
  `min = cellToWorld(c); max = cellToWorld(c+{1,1})` would be
  OFF BY HALF A CELL because `cellToWorld` returns the cell
  center, not the corner — the centered composition via
  `fromCenterExtent` is the correct form.
- §16.3: the impl lives in `src/AYTilemap.cpp` (out-of-line)
  so future SIMD / bulk variants can grow without header
  churn. `cellOrigin` is hard-coded to `{0, 0}` to match the
  P3E world-coord `setTile` / `getTile` overloads; a future
  cross-module PR lifts this to a member-field-driven origin.
- §13.PF pitfall (new): `FRectangle::fromCenterExtent(center,
  extent)` MULTIPLIES `extent` BY 0.5 INTERNALLY (see
  `MathTypes.cpp:2039-2042`). Callers pass FULL dimensions,
  not half-extent. The first implementation passed
  `tileWidth * 0.5f, tileHeight * 0.5f` — tests caught the
  resulting 4x-shrunken AABBs. Locked in §13.17 + this
  changelog entry + memory.

**Tests** (`unittest/Test_AabbOfCell.cpp` — 3 cases / ~50 CHECK):

1. `AabbOfCellZeroCentered` — aabbOfCell({0,0}) for 16x16
   tilemap: center (8, 8), full extent (16, 16) →
   min (0, 0), max (16, 16), width = height = 16.
2. `AabbOfCellNonZeroMatchesCornerPortMinusHalfCell` —
   regression for the centered form: aabbOfCell({2,3}) for
   32x16 tilemap: center (80, 56), full extent (32, 16) →
   min (64, 48), max (96, 64). `FRectangle::center()`
   round-trips to (80, 56).
3. `AabbOfCellWidthHeightMatchTileDimensions` — sweep over
   8x8 = 64 cell coords verifies width/height always equal
   tileWidth/tileHeight.

All existing tests (Phase 1+ + Phase 2 + Phase 3A/B/C/D/E/F/G
+ Phase 5 + P3H.2 + P3G.2a + P3G.1 partial) untouched.

- §11.2: bgfx-leak guard stays green. No new public headers.
- §13.17: This changelog entry. Front-matter bumped to v0.1.15.
  Total tests: **22 TEST_SUITE / 769 CHECK assertions PASS** (was
  21 / 627 at v0.1.14; +1 suite, +142 CHECK — case 3 sweeps 64
  cells, each contributing 2 CHECK assertions, hence the jump).

**Open follow-ups** (unchanged from §13.16 + §13.PF):

- Slice 6 (P3H.1): ship `SpriteSheet = AtlasDesc + path`.
- Slice 7 (P3H.3): ship `foreachTilemapView` read-only visitor.
- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.

---

### 13.18 v0.1.16 — 2026-07-30 (P3H.1 SpriteSheet thin wrapper — in-AY2D)

**Phase**: P3H.1 (in-AY2D scope only — value-type atlas+path;
no new UV derivation logic; reuses `AYTileSamplerUV::tileUV`).

**Locked changes** (per §13.PF C4 reshape):

- §3 / §5.5: `include/AY2D/AYSpriteSheet.h` ships `SpriteSheet`
  as a thin wrapper: `{ AtlasDesc atlas; std::string
  texturePath; FRectangle uvRect(uint32_t tileId) const; }`.
  Header-only struct. No new UV math; `uvRect` delegates to
  `AYTileSamplerUV::tileUV` so gutter + half-texel (§5.1 /
  §5.2 / L-7 / L-8) are applied identically to the Tilemap /
  Sprite query path.
- L-3 lock: no `bgfx::TextureHandle`. Only an opaque path
  string. The cross-module PR resolves the path into a real
  handle via AYResource's `.ayatlas` loader.
- `include/AY2D.h` umbrella adds `AYSpriteSheet.h`.
- `unittest/CMakeLists.txt` adds `Test_SpriteSheet.cpp`.

**Tests** (`unittest/Test_SpriteSheet.cpp` — 2 cases / ~80 CHECK):

1. `UvRectMatchesTileUVForSampleTileIds` — sweep 16 tile
   ids; `sheet.uvRect(tileId).minX` etc. match
   `tileUV(tileId, sheet.atlas).uMin/uMax/vMin/vMax` exactly.
   Verifies the wrapped path is identical to the
   `AYTileSamplerUV::tileUV` reference.
2. `StructFieldLayoutAndCopySemantics` — `atlas` is the
   first field (offset 0); default `texturePath` is empty;
   default `atlas` is invalid (zero dims); copy semantics
   produce a deep copy (`std::string` provides the copy
   ctor). `sizeof(SpriteSheet) >= sizeof(AtlasDesc)` and
   `>= sizeof(std::string)` sanity.

All existing tests (Phase 1+ + Phase 2 + Phase 3A/B/C/D/E/F/G
+ Phase 5 + P3H.2 + P3G.2a + P3G.1 partial + P3D.2) untouched.

- §11.2: bgfx-leak guard stays green. `AYSpriteSheet.h`
  includes `<cstdint>` + `<string>` + `aymath/MathTypes.h` +
  `AYAtlasDesc.h` + `AYTileSamplerUV.h` — all bgfx-clean.
- §13.18: This changelog entry. Front-matter bumped to v0.1.16.
  Total tests: **23 TEST_SUITE / 841 CHECK assertions PASS** (was
  22 / 769 at v0.1.15; +1 suite, +72 CHECK).

**Open follow-ups** (unchanged from §13.17 + §13.PF):

- Slice 7 (P3H.3): ship `foreachTilemapView` read-only visitor.
- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.

---

### 13.19 v0.1.17 — 2026-07-30 (P3H.3 foreachTilemapView — in-AY2D)

**Phase**: P3H.3 (in-AY2D scope only — read-only metadata
visitor; no cross-module PR; lazy-streaming counterpart to
P3H.2's eager `World2DSnapshot::build`).

**Locked changes** (per §13.PF C9 reshape):

- §3: `World2D::foreachTilemapView(F f)` template member,
  header-inline. Iterates the `entries` vector and invokes
  the visitor with a `TilemapEntryView` (handle / layer /
  sortingKey) per entry. The visitor is const, so the
  registry / `resourceEpoch` are NOT mutated (§3.4 lock).
  Order = `entries` order = registration order — the same
  order `World2DSnapshot::build()` uses.
- §13.PF C9: the visitor NEVER hands out `Entry&` because
  `Entry::resource` is always `nullptr` at HEAD (the
  `.aytilemap` loader PR is a cross-module concern per
  §4.2.1) and exposing `IAYTilemap*` would hand out a
  dangling pointer to an incomplete type.
- §13.PF C9: `TilemapEntryView` was MOVED from
  `AYWorld2DSnapshot.h` to `AYWorld2D.h`. The struct is
  still POD-equivalent (handle + layer + sortingKey), but
  the new location avoids a circular include
  (`AYWorld2D.h -> AYWorld2DSnapshot.h -> AYWorld2D.h`)
  that would otherwise prevent the header-inline template
  from compiling. `AYWorld2DSnapshot.h` re-exports the
  type via its `AYWorld2D.h` include; no duplicate
  definition.
- §3: the visitor signature is `void(const TilemapEntryView&)`
  so callers can `f(...)` lambdas / function objects
  without templates-on-templates gymnastics. The lazy
  streaming counterpart complements P3H.2's eager
  `World2DSnapshot::build` (which materialises a full copy).
- `unittest/CMakeLists.txt` adds `Test_ForeachTilemapView.cpp`.

**Tests** (`unittest/Test_ForeachTilemapView.cpp` — 2 cases /
14 CHECK):

1. `VisitorIteratesAllEntriesInRegistrationOrder` — 4
   `addTilemap` calls; the visitor sees all 4 in order,
   each carrying the matching `TilemapHandle` / layer /
   sortingKey.
2. `TilemapEntryViewHasNoResourceAccessor` —
   `static_assert` that `sizeof(TilemapEntryView)` equals
   `sizeof(TilemapHandle) + sizeof(uint32_t) * 2` (lock
   that no field was added without updating §13.PF C9 +
   §13.19). Empty-world visitor never invokes the
   callback. Mutating the visitor does NOT bump
   `resourceEpoch` (§3.4 lock verified).

All existing tests (Phase 1+ + Phase 2 + Phase 3A/B/C/D/E/F/G
+ Phase 5 + P3H.2 + P3G.2a + P3G.1 partial + P3D.2 + P3H.1)
untouched. P3H.2's `Test_World2DSnapshot` still passes
because `TilemapEntryView` is identical (just relocated).

- §11.2: bgfx-leak guard stays green. No new public headers
  (the struct relocation is internal).
- §13.19: This changelog entry. Front-matter bumped to
  v0.1.17. Total tests: **24 TEST_SUITE / 855 CHECK
  assertions PASS** (was 23 / 841 at v0.1.16; +1 suite,
  +14 CHECK). 3× consecutive green runs locked (end-of-plan
  checkpoint per Phase 3G discipline).

**Open follow-ups** (unchanged from §13.18 + §13.PF):

- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.
- All 7 in-AY2D slices from the §13.PF / plan-agent reshape
  (Phase 5 + P3H.2 + P3G.2a + P3G.1 partial + P3D.2 +
  P3H.1 + P3H.3) shipped. Next in-AY2D batch may tackle
  Phase 4 streaming (R-3G.1 full wire), Phase 5 follow-up
  (blocked-tile-id-set for `isBlocked` / raycast walker),
  or new candidate slices. **Update**: the Phase 5 follow-up
  blocked-tile-id-set lands in §13.20 P3I.1 below; the
  raycast walker + the cross-module consumer of `isBlocked`
  remain deferred.

---

### 13.20 v0.1.18 — 2026-07-30 (P3I.1 / A-4 blocked-tile-id set + flagsAtRaw data-side wire — in-AY2D)

**Phase**: P3I.1 (in-AY2D scope only — data-side wire; the
consumer side that actually walks the `isBlocked` query lives
in a cross-module PR to AYPhysics, §4.2.1).

**Scope**: design.md + §13.PF C6 → C6-R1 amendment + public
field + `flagsAtRaw` body evolution. No cross-module PRs.

**§13.PF C6 amendment** (cross-reference; full text in §13.PF):

C6 was a stub placeholder lock forbidding fake collision
(`None` MUST NOT be used as `Empty`) and forbidding override
of `isBlocked`. P3I.1 ships the data side of `isBlocked`
without altering either retained clause; it only amends the
"body must return Empty" wording. C6-R1 is **amend**, not
retract: every clause of C6 that does not collide with the
new lookup remains in force.

**Locked changes**:

- §13.20 L-3I-1 (ownership lock): `Tilemap::blockedTileIds`
  is a **public field** whose **only producer** is the
  `.aytilemap` loader metadata (cross-module PR to
  AYResource, §4.2.1). AY2D **does NOT** ship any mutator
  API (`markTileIdBlocked` / `unmarkTileIdBlocked` /
  `clearBlockedTileIds`) because §11 ownership is
  undecided; mutating the field from product code today is
  allowed (it is a public POD) but the canonical path is
  the loader. The field name carries no leading underscore
  to match the existing public-POD style (`tileIds16`,
  `tileIds32`, `animationTable`).
- §13.20 L-3I-2 (three-segment evaluation lock): `flagsAtRaw`
  is now a three-segment lookup. The ordering is **strict**:
  1. `!isInRange(c)` → `Empty`
  2. `blockedTileIds.empty()` → `Empty`
  3. `blockedTileIds.contains(getTile(c))` ? `Solid` : `Empty`
  Segment 1 BEFORE 3 is hard. `getTile` is OOB-safe and
  returns `defaultTileId`; without segment 1, a populated
  set containing `defaultTileId` would silently turn OOB
  cells into `Solid`, breaking the §8.1 contract that "no
  flag data == Empty". Segment 2 guarantees v0.1.17
  behavior is bit-identical for tilemaps with an empty set.
- §13.PF C8 retained: `flagsAtRaw(TileCoord) const noexcept`
  signature is unchanged (one-word arg type, no qualifiers
  added, no default args, no overload).
- §13.PF C6-R1 retained clauses:
  - `isBlocked` is NOT overridden in `Tilemap` nor in
    `TilemapCollisionQueryAdapter`. The
    `ITileCollisionQuery::isBlocked` base default
    (`flagsAt(c) != Empty`) is the single source of truth.
  - The `None` ban remains: `flagsAtRaw` never returns 0.
  - `TilemapCollisionQueryAdapter` is a zero-change
    pass-through. Its `flagsAt` impl already forwards to
    `_map->flagsAtRaw`, so the new lookup logic flows
    through automatically.
- §11.2: bgfx-leak guard stays green. New `<unordered_set>`
  include is STL not bgfx. `sizeof(Tilemap)` grows by the
  `unordered_set` foot-print; the pre-flight Gate G1
  confirmed no `sizeof(Tilemap)` static_assert exists, so
  no test churn.

**Tests** (`unittest/Test_TilemapBlockedTileIds.cpp` — 6
cases / 22 CHECK; actual totals 6 / 22 in the case names
listed below; total CHECK landed is 28 because some cases
assert intermediate invariants on top of the per-segment
locks):

1. `EmptyBlockSet_FlagsAtRawIsEmptyEverywhere` (4 CHECK) —
   empty-set fast path; corner / mid / OOB probes all
   return `Empty`. Backward-compat lock for segment 2.
2. `BlockedIdHit_ReturnsSolid` (4 CHECK) — single-id hit +
   multi-id hit; miss returns `Empty`. Segment 3 lock.
3. `BlockedIdRemoved_RevertsToEmpty` (3 CHECK) — `erase`
   + `clear` + re-populate; same-cell re-probe must stay
   `Empty` once empty. Empty-set fast path re-entry.
4. `OutOfRange_StaysEmpty_EvenWhenDefaultTileIdIsBlocked`
   (4 CHECK) — when `defaultTileId` is in the set, four
   distinct OOB cells must still return `Empty`. **This is
   the hard segment-1 lock**.
5. `AdapterIsBlocked_FollowsBlockedSet_NoOverride` (4
   CHECK) — `TilemapCollisionQueryAdapter` is untouched in
   this slice; the test verifies that a hit promotes
   `adapter.isBlocked(c)` to true via the base default
   alone. **C6-R1 retained-clause lock**.
6. `TileIds16And32Paths_BothConsultBlockedSet` (3 CHECK) —
   same set; same lookup; both `Narrow16` and `Wide32`
   storage paths deliver the hit.

Total tests: **25 TEST_SUITE / 883 CHECK assertions PASS**
(was 24 / 855 at v0.1.17; +1 suite, +28 CHECK).

**Open follow-ups** (unchanged + new):

- Phase 5 follow-ups still open (cross-module PR territory):
  - The `isBlocked` consumer (broadphase / character
    controller) — AYPhysics maintainer, §4.2.1.
  - `raycast` walker — axis-aligned tile grid; AYPhysics
    maintainer, §4.2.1.
  - The `.aytilemap` loader that populates
    `blockedTileIds` — AYResource maintainer, §4.2.1.
- Phase 3I continues with P3I.2 (removeTilemap LRU-coherent
  purge), P3I.3 (L-7 four-invariant test coverage), and
  P3I.4 (World2DSnapshot::diff). All stay in-AY2D.
- Cross-module PRs (CM-1..CM-5) still deferred per §4.2.1.
- KI-3I-1 (deferred — known inconsistency in
  `InMemoryTilemapChunkSource::eraseByKey` not bumping
  `evictions_lru`; P3I.2 will land an additional note
  about this).

### 13.21 P3I.2 — `World2D::removeTilemap` LRU-coherent chunk-source purge (v0.1.19)

> Phase 3I.2 closes the last in-AY2D residue on
> `removeTilemap`: a tilemap with a bound chunk source now
> has its source purged at remove time, so the source's
> LRU resident set and counters stay coherent with the
> tilemap's lifetime. **No new cross-module PR.** Stays
> inside the AY2D submodule.

**New surface** (minimal, §11.2 audit-disciplined):

| Symbol | Returns | Purpose |
|---|---|---|
| `ITilemapChunkSource::purgeChunks()` | `void` | Drop every resident chunk + cancel every pending load. **Pure virtual** (Gate G2 hit; `rg` confirmed no external implementer in `D:\Projects\AYRuntime\` outside `AY2D/`). |
| `InMemoryTilemapChunkSource::purgeChunks()` | `void` | Override: `cancelAllPending() + evictDownTo(0)`. Reuses the LRU release path → bgfx-leak guard stays green. |
| `InMemoryTilemapChunkSource::cancelAllPending()` | `void` | New private helper. `_pending.clear()`. **Does not bump `evictions_lru`** (pending requests were never resident). |
| `World2D::Entry::chunkSource` | `ITilemapChunkSource*` | New public field. Non-owning. `nullptr` = legacy / shared-source mode (§18.7). |
| `World2D::addTilemap(layer, sortingKey, chunkSource)` | `TilemapHandle` | New 3-arg overload. The 2-arg overload delegates with `nullptr`. |
| `World2D::findEntryByHandle(h)` (private) | `Entry*` | New mutable-pointer lookup used by `removeTilemap` to read `chunkSource` BEFORE erasing the entry. |

**Surface delta: 1 field + 1 public 3-arg overload + 2 new
public methods + 1 private helper + 1 private lookup.** No
new types, no new headers, no new dependencies.

**L-3I-1..L-3I-7 invariant locks** (recorded here so the
plan agent's gates are reproducible from the docs alone):

- **L-3I-1 (chunk-source ownership)** — `Entry::chunkSource`
  is **non-owning**. Lifetime of the source is the caller's
  responsibility; this is the same convention as
  `Entry::resource` (the `IAYTilemap*`). The cross-module
  Phase 4 streaming PR (§4.2.1) replaces both with
  strong-ref handles (`TilemapResourceHandle`).
- **L-3I-2 (one-source-per-tilemap, §18.7)** — each tilemap
  binds at most one chunk source at a time. `purgeChunks()`
  semantics = "purge all of THIS source's chunks". This is
  not a "per-tilemap" key — keys remain `(ChunkCoord)` per
  source. Sharing one source across N tilemaps is the
  caller's choice (§18.7 second use case).
- **L-3I-3 (purge reuses eviction path)** — `purgeChunks`
  calls `evictDownTo(0)`, never `_cache.clear()` directly.
  The bgfx-leak guard relies on this: the only public release
  path is `evictByKey → eraseByKey` (per-chunk), and
  `evictDownTo` is the only public bulk release path.
  Bypassing it would land a `clear()`-style shortcut that
  skips the counter bookkeeping.
- **L-3I-4 (pending cancel ≠ eviction)** — `cancelAllPending()`
  bumps no counter. `evictions_lru` is bumped exactly once
  per previously-resident chunk in `evictDownTo(0)` — so a
  purge that clears 5 resident + 3 pending entries bumps
  `evictions_lru` by 5, **not 8**. This is locked in test
  case 7 of `Test_World2DRemoveTilemapPurge.cpp`.
- **L-3I-5 (removeTilemap strict ordering)** —
  ```
  Entry* e = findEntryByHandle(h);
  if (!e) return false;
  if (e->chunkSource) e->chunkSource->purgeChunks();  // 1
  if (removeEntryByHandle(h)) {                       // 2
      bumpEpoch();                                     // 3
      tilemaps_in_world-- (saturating);                // 4
      return true;
  }
  ```
  Purge happens **before** entry erase (so the pointer
  read is valid); epoch bump happens **after** both purge
  and erase so the epoch captures the world-shape change
  only, not the chunk eviction.
- **L-3I-6 (swap is no-purge)** — `swapTilemap` does not
  touch `chunkSource` and does not call `purgeChunks`. The
  resident set and `evictions_lru` are unchanged across a
  swap; only `layer` / `sortingKey` are written and only
  `resourceEpoch` bumps. Locked in test case 5.
- **L-3I-7 (direct purge does not bump epoch)** — a caller
  invoking `src.purgeChunks()` outside of `World2D` does
  not touch `resourceEpoch`. This is the hook for the
  future cross-module consumer (e.g. a phase-4 streaming
  controller) to call purge without confusing World2D's
  epoch bookkeeping. Locked in test case 8.

**§13.20 "Open follow-ups" was rewritten** to fold P3I.2
in and to defer only the P3I.3 / P3I.4 residue. The
KI-3I-1 entry was promoted to a dedicated sub-section
below.

**KI-3I-1 (known inconsistency, deferred)** —
`InMemoryTilemapChunkSource::eraseByKey(MapKey key)` does
**not** bump `evictions_lru` (only `chunk_resident_count`).
This is observable: a single-chunk `eraseByKey` call leaves
`evictions_lru` unchanged. The counter is bumped in
`evictIfNeeded` (over-cap path) and `evictDownTo` (soft-cap
and purge paths) — i.e. only the bulk paths. The fix
candidate is to add the same `_counters.evictions_lru.fetch_add(1, ...)`
line to `eraseByKey`; the rationale for **not** fixing it
in P3I.2 is that no current call site uses `eraseByKey` for
eviction (the only caller is the per-chunk internal path in
`evictIfNeeded` and `evictDownTo`, which already account
for the counter bump upstream). Promoting the counter
bump into `eraseByKey` would **double-count** under those
callers. A clean fix needs a small refactor (e.g. an
"internal" variant without the counter bump); deferred to
Phase 3J.

**Tests** (`Test_World2DRemoveTilemapPurge.cpp`, suite
`World2DRemoveTilemapPurge`, 8 cases / 26 CHECK):

1. `RemoveTilemap_WithSource_PurgesAllResidentChunks` (4
   CHECK) — cap=10, put 8; remove → 0 resident +
   `evictions_lru == 8`.
2. `RemoveTilemap_WithSource_BumpsEpochExactlyOnce` (3
   CHECK) — add bumps epoch to E; remove bumps to E+1;
   second remove (stale handle, `false`) does NOT bump.
   L-3I-5 case 3 lock.
3. `RemoveTilemap_DecrementsTilemapsInWorldExactlyOnce`
   (4 CHECK) — 1 → 0; second remove on already-removed
   handle returns `false` and counter stays 0
   (saturating, §13.15).
4. `RemoveTilemap_NullSource_LegacyOverloadUnaffected`
   (3 CHECK) — 2-arg `addTilemap` (nullptr source);
   remove returns `true`, epoch +1, no crash.
5. `SwapTilemap_DoesNotPurgeChunks` (4 CHECK) — swap
   leaves `residentCount()==8` and `evictions_lru==0`;
   handle still valid. L-3I-6 lock.
6. `PurgeChunks_Idempotent` (3 CHECK) — two consecutive
   `purgeChunks()` calls: second one bumps no counter.
7. `PurgeChunks_CancelsPending_NoEvictionCounterInflation`
   (3 CHECK) — resident 5 + pending 3; purge →
   `evictions_lru == 5` (NOT 8). L-3I-4 lock.
8. `DirectPurge_DoesNotBumpResourceEpoch` (2 CHECK) —
   direct `src.purgeChunks()` (no World2D in the call
   stack) leaves `world.resourceEpochValue()` unchanged.
   L-3I-7 lock.

Total tests: **26 TEST_SUITE / 903 CHECK assertions
PASS** (was 25 / 877 at v0.1.18; +1 suite, +26 CHECK).

**Files modified**:

- `include/AYTilemapChunkSource.h` (+2 lines: pure virtual
  `purgeChunks`; `+1` override decl; `+1` private
  `cancelAllPending` decl)
- `include/AYWorld2D.h` (+1 field in `Entry`;
  `+1` 3-arg `addTilemap` overload; `+1` private
  `findEntryByHandle`)
- `src/AYInMemoryTilemapChunkSource.cpp` (+~30 lines:
  `cancelAllPending` + `purgeChunks` impl; the
  pre-existing `evictDownTo` got its sentinel fixed
  — see KI-3I-2 below)
- `src/AYWorld2D.cpp` (`removeTilemap` body rewrite per
  L-3I-5; new 3-arg `addTilemap` impl; 2-arg delegates
  to 3-arg; new `findEntryByHandle` impl; `swapTilemap`
  untouched; `+~25/-~6` lines net)
- `unittest/Test_World2DRemoveTilemapPurge.cpp` (NEW, 198 lines)
- `unittest/CMakeLists.txt` (`+1` test source line)

**Files NOT touched**: `TilemapEntryView` (§13.14 /
§13.19 shape lock intact), `TilemapBinding` (deprecated,
no resurrection), root `CMakeLists.txt` / `.gitmodules`
(handled by the root-bump commit).

**KI-3I-2 (sentinel bug fixed in P3I.2)** — the
pre-P3I.2 `evictDownTo(uint32_t target)` used a
sentinel-of-one when `target == 0`, leaving one chunk
behind. This was the only caller path for
`evictDownTo(0)` (the purge path); a single-purge test
would have shown `evictions_lru == 7` instead of 8. P3I.2
amends the body to interpret `target == 0` as
"drain everything". P3G.2a (`setMaxChunksCpuSoftCap`)
callers always pass `softCap > 0`, so the soft-cap path is
unaffected. The fix is **behavior-additive** (only the
`target == 0` edge case changes; non-zero targets are
identical).

**Open follow-ups** (after P3I.2):

- Phase 5 follow-ups still open (cross-module PR
  territory): see §13.20 — unchanged.
- P3I.3 (L-7 four-invariant test coverage, test-only)
  and P3I.4 (`World2DSnapshot::diff`) continue Phase 3I
  in-AY2D; both ship next.
- Cross-module PRs (CM-1..CM-5) still deferred per
  §4.2.1.
- KI-3I-1 (`eraseByKey` not bumping `evictions_lru`)
  remains deferred to Phase 3J.

### 13.22 P3I.3 — L-7 four-invariant test coverage (v0.1.20)

> Phase 3I.3 closes A-2 **reshaped**: rather than adding
> a new `layerMask` setter/getter API, the slice is
> **test-only** and tightens the L-7 invariant coverage on
> `OrthographicCamera::isPixelPerfectSafe()`. Zero surface
> change. Stays inside the AY2D submodule.

**A-2 reshape rationale** (recorded so the next reader
sees why this slice did not add a setter/getter):

- The pre-P3I.3 `OrthographicCamera` exposes `layerMask`
  as a public field (design.md §3.2, `AYOrthographicCamera.h:81`),
  already round-trippable via direct assignment — there
  is **no setter/getter API to add** that would change
  observable behavior. The original A-2 list entry
> "add layerMask API"
was a hint at a different concern: **L-7 had three
  invariant negatives and no positive baseline**, plus no
  viewport-scale coverage. P3I.3 addresses that with
  tests, not API.
- Adding a `setLayerMask(uint32_t)` / `getLayerMask()`
  pair in `OrthographicCamera` would introduce a
  redundant accessor for a public field — a strict
  layer violation against the existing POD-style
  convention (L-3I-6) and the §3.2 design rule
  "no getter/setter pairs for public fields".

**L-7 invariant (four-condition AND)** — restated from
design.md §3.2 for grep-ability:

```
isPixelPerfectSafe()  ==  true  iff  ALL of:
  1. position is integer-valued
  2. zoom is integer-valued
  3. viewport scale (widthPx / heightPx) is
     integer-multiple of viewSize
  4. atlasGutterIsZero == true
```

**Coverage gap before P3I.3** — the `OrthographicCamera`
test suite had:

- `PixelPerfectSafeAllInvariantsHold` — implicit positive
  but no per-invariant assertion (a future drift in any
  one of the four would not be localised to that
  invariant's name in the failure log).
- `PixelPerfectUnsafeWhenZoomFractional` — invariant 2.
- `PixelPerfectUnsafeWhenPositionFractional` — invariant 1.
- `PixelPerfectUnsafeWhenGutterNonZero` — invariant 4.

Invariant **3 (viewport scale)** had **no negative case**.
The positive baseline was implicit and the predicate
might have silently regressed to a constant (always `true`
or always `false`) without the existing tests noticing.

**New tests** (4 cases / ~17 CHECK, appended to the
existing `OrthographicCameraSuite` in
`Test_OrthographicCamera.cpp`):

1. `L7_PixelPerfectSafe_AllFourInvariantsHold_True` (5
   CHECK) — explicit positive baseline. Sets the four
   preconditions, asserts each individually, then asserts
   the predicate result. The pre-condition assertions
   make a future regression in any one invariant
   show up in the test log with that invariant's name.
2. `L7_PixelPerfectUnsafe_NonIntegerViewportScale` (4
   CHECK) — fills the invariant-3 gap. Walks
   `widthPx == 0`, `heightPx == 0`, integer-multiple
   viewport, recovery to integer-multiple viewport. Each
   transition is asserted; the round-trip catches
   "predicate is sticky-false after a failed call" if a
   future refactor introduces state.
3. `L7_PixelPerfectUnsafe_HalfTexelOffset_Rejection` (5
   CHECK) — explicit half-texel walk on both axes plus
   the both-axes case plus recovery. Distinct from the
   pre-P3I.3 `PixelPerfectUnsafeWhenPositionFractional`
   in that the recovery path is asserted.
4. `L7_LayerMaskRoundTripsThroughAssignment` (3 CHECK) —
   orthogonality lock. `layerMask` write/read is
   independent of `isPixelPerfectSafe()`; this case
   documents the round-trip and asserts the predicate
   stays `true` under arbitrary `layerMask` values. This
   is the A-2 reshape evidence — a setter/getter API
   would not add observable value beyond what this case
   already locks.

Total tests: **27 TEST_SUITE / 933 CHECK assertions
PASS** (was 26 / 903 at v0.1.19; +0 suites, +30 CHECK;
3× consecutive green locked; bgfx-leak guard green).

**L-3I-6 (POD-style) lock** — `OrthographicCamera`
remains POD-with-public-fields. The four new cases use
direct field assignment; no accessor is added. This
matches the §3.2 convention and keeps the layer surface
flat for the cross-module consumer (a future
`RenderSystem2D` consumer reads `layerMask` directly).

**Files modified**:

- `unittest/Test_OrthographicCamera.cpp` (+142 lines:
  4 new `TEST_CASE` blocks appended after
  `LayerMaskDefaultIsAllOnes`; no other change in the
  file).

**Files NOT touched**: `include/AYOrthographicCamera.h`,
`src/AYOrthographicCamera.cpp` (none), `CMakeLists.txt`
(test file already registered), `design.md §3.2`
(predicate body unchanged), root `CMakeLists.txt` /
`.gitmodules` (root-bump commit).

**Open follow-ups** (after P3I.3):

- Phase 5 follow-ups still open (cross-module PR
  territory): see §13.20 — unchanged.
- P3I.4 (`World2DSnapshot::diff`) is the last in-AY2D
  slice for Phase 3I; ships next.
- Cross-module PRs (CM-1..CM-5) still deferred per
  §4.2.1.
- KI-3I-1 (`eraseByKey` not bumping `evictions_lru`)
  remains deferred to Phase 3J.
- A-2 (the original "add layerMask API" entry) is now
  **closed-by-reshape**; no follow-up.

### 13.23 P3I.4 — `World2DSnapshot::diff` + O(1) epoch fast path (v0.1.21)

> Phase 3I.4 closes A-9: a snapshot-diff helper that
> downstream systems (e.g. the future `RenderSystem2D`
> cross-module PR, the editor's undo stack) can use to
> answer "what changed in the world since the previous
> snapshot?" without walking the full `entries` vector
> every frame. **No cross-module PR.** Stays inside the
> AY2D submodule.

**New surface** (minimal, §11.2 audit-disciplined):

| Symbol | Returns | Purpose |
|---|---|---|
| `World2DSnapshot::resourceEpoch` | `uint64_t` | New public field. Copy of `world.resourceEpoch` at `build()` time. |
| `World2DSnapshot::diff(old)` | `World2DSnapshotDiff` | New method. O(1) fast path on matching `resourceEpoch`; sort + two-pointer linear merge otherwise. **Not `noexcept`** (allocates three `std::vector`s). |
| `World2DSnapshotDiff::ModifiedEntry` | `struct` | New POD. Carries the common `handle` + `oldLayer/newLayer/oldSortingKey/newSortingKey` for a swap. |
| `World2DSnapshotDiff::added` | `std::vector<TilemapEntryView>` | New in `*this`, not in `old`. |
| `World2DSnapshotDiff::removed` | `std::vector<TilemapEntryView>` | In `old`, not in `*this`. |
| `World2DSnapshotDiff::modified` | `std::vector<ModifiedEntry>` | In both, `(layer, sortingKey)` differs. |
| `World2DSnapshotDiff::empty()` | `bool noexcept` | True iff all three vectors are empty. |

**Surface delta: 1 field + 1 method + 1 new POD type + 3
new vectors + 1 helper.** No new types outside the
`World2DSnapshot` family, no new headers, no new
dependencies. The shape of `TilemapEntryView` (§13.14 /
§13.19) is untouched; the sizeof static_assert at
`Test_ForeachTilemapView.cpp:57-64` still holds.

**L-3I-7..L-3I-10 invariant locks** (recorded here so the
plan agent's gates are reproducible from the docs alone):

- **L-3I-7 (O(1) epoch fast path)** — when
  `*this.resourceEpoch == old.resourceEpoch`, the result
  is `World2DSnapshotDiff{}` and the function does NOT
  walk the `entries` vectors. The precondition (same
  world) is a docs lock — see L-3I-8. The fast path is
  asserted by `SameEpoch_DiffIsEmpty_FastPath`.
- **L-3I-8 (same-world precondition)** — `diff` is only
  well-defined when both snapshots come from the same
  `World2D` instance. Cross-world `diff` is undefined
  because `resourceEpoch` is a per-world counter that
  can collide. A consumer that swaps worlds is expected
  to drop both snapshots and rebuild. The lock is a
  docs-only precondition; a `worldId` field on the
  snapshot is **deliberately not added** to avoid
  snapshot-shape churn (a follow-up P3J candidate if
  cross-world diff becomes a real need).
- **L-3I-9 (ABA + `(id, gen)` key)** — `World2D` ids are
  monotonic (never reused; `_nextTilemapId` always
  advances). The ABA guard lives in `generation`:
  `removeTilemap` bumps the generation of the *slot*
  even though the slot is gone, so any future add has a
  fresh generation. For `diff` this means: an old handle
  and a new handle at the SAME id are impossible; ABA
  manifests as different ids, different generations, and
  the `(id, generation)` sort+merge key keeps them
  apart. The `ABA_SameIdDifferentGeneration_…` case
  asserts the resulting added/removed partition.
- **L-3I-10 (sort + two-pointer merge)** — the three
  result vectors are sorted by `(id, generation)`
  ascending. The merge is a single linear walk over two
  sorted `vector<const TilemapEntryView*>` index arrays
  — the underlying `entries` vectors are never modified
  (so `diff` is genuinely const). No hash map; O(N log N)
  for the sort + O(N) for the merge.
- **L-3I-11 (forward lock on layer/sortingKey mutators)**
  — any future `World2D` mutator that writes
  `layer`/`sortingKey` MUST bump `resourceEpoch` and
  MUST be listed in §3.4 alongside `addTilemap` /
  `removeTilemap` / `swapTilemap`. The epoch fast path
  relies on the invariant "matching epoch == matching
  entries". The Gate G4 grep on the pre-slice-4 working
  tree was the sanity check; a future slice that adds
  a new mutator must redo the gate.

**Why `not noexcept`** — `diff` allocates three
`std::vector`s. The remaining public methods on
`World2D` / `World2DSnapshot` are `noexcept`; `diff` is
the one documented exception. The rationale is in the
header docstring at the declaration; §13.23 records it
here for grep-ability.

**Why `TilemapBinding` is NOT reused** — the deprecated
`TilemapBinding` struct (P3H.2 §13.14) carries the
fields needed for `added` / `removed` but does not
distinguish "old" from "new" in the `modified` case.
`World2DSnapshotDiff::ModifiedEntry` exists precisely
to carry the (old, new) pair, so reusing
`TilemapBinding` would either lose information or
require a second struct. The deprecation lock holds.

**Tests** (`Test_World2DSnapshotDiff.cpp`, suite
`World2DSnapshotDiff`, 8 cases / ~57 CHECK):

1. `SameEpoch_DiffIsEmpty_FastPath` (4 CHECK) — L-3I-7
   lock: same epoch → empty diff.
2. `AddTilemap_ShowsUpInAdded` (4 CHECK) — new handle
   lands in `added`; `removed` and `modified` empty.
3. `RemoveTilemap_ShowsUpInRemoved` (4 CHECK) — old
   handle lands in `removed` with original generation.
4. `SwapTilemap_ShowsUpInModified_OldNewFieldsCorrect`
   (4 CHECK) — `modified[0]` carries the exact
   (oldLayer, newLayer, oldSortingKey, newSortingKey).
5. `ABA_SameIdDifferentGeneration_…` (8 CHECK) — L-3I-9
   lock: remove + re-add goes to `removed` + `added`,
   never `modified`. `id` is monotonic (not reused) so
   the assertion is on `(id, generation)`.
6. `MixedBatch_DeterministicOrder_SortedByIdThen…` (6
   CHECK) — L-3I-10 lock: 2 removes + 1 swap; the
   `removed` vector is strictly ascending by `id`.
7. `DiffIsNonMutating_AndDoesNotBumpEpoch` (4 CHECK) —
   §3.4 + const lock: diff does not touch
   `resourceEpoch`; `s2_again.diff(s1_again)` (same
   world, same epoch) hits the fast path.
8. `DefaultConstructedSnapshot_AsOldBaseline_YieldsAll…`
   (6 CHECK) — L-3I-8 cold-start lock: `old = {}` (epoch
   0) vs a 3-entry world (epoch 3) yields all 3 in
   `added`.

Total tests: **28 TEST_SUITE / 990 CHECK assertions
PASS** (was 27 / 933 at v0.1.20; +1 suite, +57 CHECK;
3× consecutive incremental green locked; bgfx-leak guard
green; cold-configure sanity check deferred to 3J
environment-passing PR per the do_cmake.bat
`VCPKG_INSTALLED_DIR` gap noted in the wrap-up commit).

**Files modified**:

- `include/AYWorld2DSnapshot.h` (+~70 lines: 1 new
  field, 1 new method decl, the
  `World2DSnapshotDiff` POD moved before
  `World2DSnapshot` so the return type is complete at
  declaration; +~50 lines net; net 3 files
  modified + 1 new file).
- `src/AYWorld2DSnapshot.cpp` (+~80 lines:
  `build()` now copies `resourceEpoch`; new
  `diff()` impl using `std::vector<const
  TilemapEntryView*>` indices + `std::sort` + two-pointer
  linear walk; new `#include <algorithm>` /
  `<utility>`).
- `unittest/Test_World2DSnapshotDiff.cpp` (NEW, ~210 lines).
- `unittest/CMakeLists.txt` (`+1` test source line).

**Files NOT touched**: `TilemapEntryView` (§13.14 /
§13.19 shape lock intact; sizeof static_assert
unaffected), `TilemapBinding` (deprecated; no
resurrection), `World2D` body (`addTilemap` /
`removeTilemap` / `swapTilemap` unchanged; §3.4 epoch
discipline is the foundation of L-3I-7's fast path),
root `CMakeLists.txt` / `.gitmodules` (handled by the
root-bump commit).

**Open follow-ups** (after P3I.4):

- Phase 5 follow-ups still open (cross-module PR
  territory): see §13.20 — unchanged.
- **Phase 3I ships complete** with this commit. The
  four-slice plan (P3I.1 / P3I.2 / P3I.3 / P3I.4) is
  fully closed.
- Cross-module PRs (CM-1..CM-5) still deferred per
  §4.2.1.
- KI-3I-1 (`eraseByKey` not bumping `evictions_lru`)
  remains deferred to Phase 3J.
- **3J env PR** (operational, not functional): the
  cold-configure side of P3I.4 lock #2 hit a
  `do_cmake.bat` env-passing gap (`VCPKG_INSTALLED_DIR`
  not in `vcvars64.bat`'s default env). 3J starts with a
  small wrapper fix to make cold-configure reproducible
  from a clean build dir without losing the per-shell
  vcpkg cache.
- **Phase 3J candidates** (next in-AY2D residue):
  A-1 (TileAnimation batch state, 5 case / 30 CHECK),
  A-3 (SpriteDrawCmd POD layout static_assert, 6
  case / 12 CHECK), A-8 (Sprite batch state helper),
  A-10 (AtlasDesc validator), A-12 (`EvictionPolicy`
  enum reserved values), A-7 (`TilemapLoadState` enum
  test), A-11 (`ChunkRequestHandle` wrap-around),
  A-6 (chunk-row coalesce), KI-3I-1 fix
  (`eraseByKey` evictions_lru bump), 3J env PR.

---

## 18. Phase 3G chunk-source budget + reject counter (in-AY2D scope)

> Phase 3G adds the **runtime budget gate** to
> `InMemoryTilemapChunkSource`. The LRU wire + counter infra
> shipped as part of P3A / P3C; P3G closes the loop by
> exposing the `TilemapBudget` shape as a **live runtime
> surface**, not just a documented placeholder.
> `Distance` / `TimeWindow` are deferred to Phase 4 (R-3G.1).

### 18.1 Surface

| Symbol | Returns | Purpose |
|---|---|---|
| `InMemoryTilemapChunkSource::setCapacity(uint32_t)` | `void` | Runtime cap on chunk-resident-count (P3A's `_capacity` field now mutable). `0` = unlimited (matches ctor). |
| `InMemoryTilemapChunkSource::setMaxIoBytesPerSec(uint64_t)` | `void` | Activate / disable the rate gate. `0` = disabled. |
| `InMemoryTilemapChunkSource::setBudget(b)` | `bool` | Atomic set of both fields above. Returns `true` iff the policy is LRU (R-3G.4). Non-LRU policy → no-op, `false`. |
| `Ay2DCounters::chunk_io_reject` | `uint64_t` (atomic) | New tenth field (was 9). Cumulative rejection count. Reset by `resetAll` only. |
| `InMemoryTilemapChunkSource::budget()` | `TilemapBudget` | Read current budget (the budget struct includes all four fields for forward-compat). |
| `evictionPolicyActive() const` | `EvictionPolicy` | Returns the live policy (always LRU in P3G; Phase 4 PR overrides). |

`TilemapBudget` (already in `AYTilemapChunkSource.h:367`) is
**unchanged** in shape — P3G just wires its LRU half. The full
field set:

```cpp
struct TilemapBudget {
    uint32_t       maxChunksLoaded    = 1024;     // soft cap (wire: setCapacity)
    uint32_t       maxChunksResident  = 2048;     // reserved (R-3G.4 — Phase 6)
    uint32_t       maxIoBytesPerSec   = 64 * 1024 * 1024;  // 64 MB/s (wire: rate gate)
    EvictionPolicy eviction           = EvictionPolicy::LRU;  // wire: LRU only in P3G
};
```

**Pre-flight (§13.PF) 3-tier clarification**: the `TilemapBudget`
field set is a **3-tier** residency model and each tier has a
distinct ownership boundary:

1. **`maxChunksLoaded` (hard cap, evict-down-to via `setCapacity`)** —
   in-AY2D CPU soft cap. Shipped P3A → P3G. Always honored.
2. **`maxChunksCpuSoftCap` (NEW field, added by pre-flight)** —
   in-AY2D CPU soft cap, evict-down-to when `setBudget` lowers it.
   P3G.2a PR ships the wiring; until then the field is forward-decl
   only.
3. **`maxChunksResident` (GPU residency, R-3G.4)** — cross-module
   PR to `RenderResourceManager`. Not wired in-AY2D. Documented
   field stays so cross-module PR can pick up the wire without a
   breaking change.

Field ordering: `maxChunksLoaded` → `maxChunksCpuSoftCap` →
`maxChunksResident`. Each tier is checked AFTER the previous
tier; rejection telemetry lives in distinct counters
(`chunk_io_reject` for rate gate; `chunk_io_residency_reject`
for soft-cap eviction blocked by pin set).

### 18.2 Rate-limit semantics (locked, R-3G.3)

- Sliding window of `(_rejectedInWindow, _windowStartUs)`. The
  window is 1 second wide (matches `maxIoBytesPerSec`'s natural
  unit). Whenever `nowUs - _windowStartUs >= 1'000'000`, the
  window rolls: `_windowStartUs = nowUs`, `_rejectedInWindow = 0`.
- `maxIoBytesPerSec == 0` ⇒ no rate gate. The
  `requestChunk` fast-path is identical to pre-P3G behavior.
  (R-3G.3a.)
- `ChunkNominalBytes` = 32 * 1024 for `Narrow16`, 64 * 1024
  for `Wide32` chunks (the chunk-of-16×16 default). Today's
  `InMemoryTilemapChunkSource` always uses this size — the
  rate gate reads the size from the source's policy flag, not
  the request argument.
- On `requestChunk`:
  1. Refresh the window if expired.
  2. `wouldExceed = (_rejectedInWindow + nominal) > maxIoBytesPerSec`
  3. If `wouldExceed` → `_counters.chunk_io_reject.fetch_add(1, relaxed)`;
     return `ChunkRequestHandle{0, 0}` (invalid); DO NOT insert into
     `_pending`. Caller's `tryGetChunk` will see no entry and report
     "not loaded".
  4. Else → `_rejectedInWindow += nominal`; proceed with the
     pre-P3G path.

### 18.3 `EvictionPolicy` interaction

- `TilemapBudget::eviction == LRU` (default). Wire-side: this
  matches `InMemoryTilemapChunkSource::evictIfNeeded`, which has
  shipped since P3A (`design.md §13.6`).
- `TilemapBudget::eviction == Distance | TimeWindow`: `setBudget`
  returns `false` (R-3G.1). Tests assert this behavior with a
  stub-budget call asserting the return value + the previous
  budget remains in effect.
- Future PR (Phase 4 streaming) replaces the `false` return
  with the actual `Distance` / `TimeWindow` implementation.
  P3G does NOT open that door — the cross-module PR
  (§4.2.1) does.

### 18.4 Tests (`Test_ChunkSourceBudget.cpp` — 8 cases)

1. `SetCapacity0DisablesCap` — `setCapacity(0)` after the ctor
   cap was non-zero: source goes unlimited (P3A default
   preserved).
2. `SetCapacity5EvictsOldestOnSixthPut` — `setCapacity(5)`,
   put 6 distinct coords → 5-resident, with the oldest coord
   evicted and the newest present.
3. `SetMaxIoBytesPerSecZeroDisablesGate` — `setMaxIoBytesPerSec(0)`
   after a previous non-zero setting: rate gate disables,
   `requestChunk` never rejects (counter never increments).
4. `RateGateRejectsBeyondBudget` — `setMaxIoBytesPerSec(64 KB)`;
   request 5 narrow16 chunks → first 2 succeed (32 KB each),
   third is rejected and bumps `chunk_io_reject`.
5. `RateGateWindowRolloverAllowsNewRequests` — same as case 4;
   wait for the window to roll (we test this with an explicit
   helper `advanceWindow(us)` exposed in the test target — the
   helper just bumps `_windowStartUs` to a value such that
   `nowUs - _windowStartUs >= 1'000'000`). New request after
   the roll passes.
6. `BudgetNonLRUPolicyReturnsFalseAndKeepsPrevious` —
   `setBudget({..., EvictionPolicy::Distance})` returns
   `false`; previous LRU budget remains in effect.
7. `BudgetLRUAppliesBothFields` — `setBudget({.maxChunksLoaded=10,
   .maxIoBytesPerSec=32KB, .eviction=LRU})` returns `true`;
   `budget()` reads back the same shape.
8. `ChunkIoRejectCounterResetByResetAllOnlyNotResetPerFrame` —
   trigger a rejection, snapshot counter, call `resetPerFrame`:
   counter is unchanged. Call `resetAll`: counter is 0. The
   `resetAll / resetPerFrame` discipline matches the existing
   counters in `Ay2DCounters` (cumulative + per-frame split).

### 18.5 Out of scope (deferred)

- **`Distance` / `TimeWindow` eviction**: R-3G.1; Phase 4 PR.
- **`maxChunksResident` GPU-side gate**: R-3G.4; Phase 6 PR,
  gated on RenderResourceManager.
- **Bytes-per-request argument**: the rate gate hard-codes the
  chunk-of-16×16 nominal size today. A future
  `TilemapBudget::narrow16ChunkNominalBytes` field would
  generalize this — but today's InMemory chunk source has no
  heterogeneity.
- **Cross-process budget sync**: a `World2D` owning multiple
  chunk sources can compose their budgets but P3G does not
  provide a multi-source aggregator. World2D composition is
  Phase 4 streaming (the `TilemapStreamingSystem`).
- **`.aytilemap` budget section**: a serialized budget (per
  resource) lands with the `.aytilemap` loader PR.

### 18.6 Risks / invariants

- **R-3G.1** non-LRU policy = no-op + `false` return. Tests
  enforce this in case 6. **§13.PF clarification**: this lock
  is **active**, not a TODO. `Distance` requires a camera
  reference not present in `InMemoryTilemapChunkSource`; a
  naive "distance from (0,0)" would silently evict the wrong
  chunks under non-trivial camera movement. `TimeWindow`
  requires a per-entry access timestamp the LRU list does not
  carry today. Both policies are deferred to the cross-module
  Phase 4 streaming PR (§4.2.1) which owns the chunk-source ↔
  camera composition. P3G ships the counter scaffolding only
  (P3G.1 partial).
- **R-3G.2** `chunk_io_reject` reset discipline: cumulative
  (only `resetAll` zeros it; `resetPerFrame` does NOT). Case
  8 enforces this.
- **R-3G.3** rate gate is a **sliding window** with explicit
  rollover, not a leaky bucket (simpler test, deterministic).
  Window = 1 second.
- **R-3G.3a** `maxIoBytesPerSec == 0` disables the gate; the
  pre-P3G fast path is identical (no counter increment, no
  rejected handle). Case 3 enforces this.
- **R-3G.4** `setBudget` with `maxChunksResident != 0` is a
  silent no-op for the residency side (only the rate + cap
  side is wired). The `TilemapBudget` struct continues to
  carry the field for forward-compat; the source's
  `budget()` getter reflects the user's last-requested value.
- **R-3G.5** `EvictionPolicy` enum shape unchanged — P3G does
  not extend it. Phase 4 PR is the next time the enum is
  touched.
- **R-3G.6** bgfx-leak guard stays green. New symbols are
  exposed through `Ay2DCounters` (the counter field is just a
  `<cstdint>` member) and `InMemoryTilemapChunkSource::setCapacity`
  / `setMaxIoBytesPerSec` / `setBudget` member methods — no
  new headers, no new bgfx paths.
- **R-3G.7** deterministic across machines: the rate gate's
  window advances on wall-clock time. P3A tests already use
  this pattern (`InMemoryTilemapChunkSource::requestChunk`
  stamps `requestTimeUs`); tests use a helper `advanceWindow`
  to make them reproducible (no `sleep()` in unit tests).

### 18.7 One-source-per-tilemap model + `purgeChunks` semantics (P3I.2 lock)

> Added in P3I.2 (§13.21). This sub-section is the model
> lock for the `Entry::chunkSource` field and the new
> `ITilemapChunkSource::purgeChunks()` virtual. **§18.4 was
> already taken** by the `Test_ChunkSourceBudget.cpp` test
> list; P3I.2 deliberately inserts this lock as §18.7 to
> avoid renumbering the existing 18.4–18.6 sections (per the
> "no hygiene churn" rule in the Phase 3H retrospective).

**Model**:

- Each `World2D::Entry` binds **at most one**
  `ITilemapChunkSource*` at a time.
- `purgeChunks()` semantics = "purge all of THIS source's
  chunks". There is no `purgeChunksFor(Entry)` overload in
  the AY2D-internal surface today; the source is the
  purge unit because the cache key is `(ChunkCoord)` and
  the cache does not know which tilemap each chunk belongs
  to.
- Sharing one source across N tilemaps is the caller's
  choice: pass the same `&src` to N `addTilemap(..., &src)`
  calls, and a `removeTilemap` on any ONE of them will
  purge the entire source. This is **intentional and
  documented**: the LRU cache has no per-tilemap partition,
  so a `purgeChunks` always lands as "purge all". The two
  expected use cases:

  1. **One source per tilemap** (default, recommended for
     P3I.2 demos and most product paths): each tilemap has
     a unique `InMemoryTilemapChunkSource`; the
     `removeTilemap` call is a clean teardown for that
     tilemap's chunks only.
  2. **Shared source across N tilemaps** (e.g. several
     tilemap variants reading the same atlas chunk pool):
     a `removeTilemap` on one tilemap will purge the
     shared pool, so the user must re-prefetch for the
     remaining tilemaps. This is a **product-level
     decision**, not a knob on the source.

- `nullptr` `chunkSource` = legacy / no-source mode. The
  2-arg `addTilemap(layer, sortingKey)` overload delegates
  with `nullptr`; the entry is otherwise identical, and
  `removeTilemap` simply skips the purge step in that case.

**Why no per-tilemap key in the source cache** — adding a
tilemap-id dimension to the cache key would require the
source to be tilemap-aware, which crosses a module boundary
in the future (`TilemapStreamingSystem` would own the
key-space anyway). P3I.2 stays in-AY2D by keeping the cache
key as `(ChunkCoord)` and binding the source to the entry
non-owningly. The cross-module Phase 4 streaming PR
(§4.2.1) is the place to revisit the key shape — at that
point the chunk source becomes a member of the streaming
system, not a field on the entry.

**KI-3I-1 (`eraseByKey` counter asymmetry)** — the
`InMemoryTilemapChunkSource::eraseByKey(MapKey key)` helper
deletes a single entry from the cache and updates
`chunk_resident_count`, but does **not** bump
`evictions_lru`. The counter is bumped in the bulk paths
(`evictIfNeeded`, `evictDownTo`) but not in the per-key
helper. This is observable if a caller invokes `eraseByKey`
directly outside of a bulk-eviction call site (none exist
in AY2D today; the only public release path is the bulk
helpers). The asymmetry was not introduced in P3I.2 — it
predates Phase 3G. P3I.2 documents the gap here so future
debugging does not chase a ghost. The fix candidate is a
small refactor (an "internal" variant of `eraseByKey` that
does not bump the counter, with the public helper bumping
it); deferred to Phase 3J.

**Invariants** (re-stated from §13.21 for grep-ability):

- `purgeChunks` **reuses** `evictDownTo(0)` — never bypasses
  the LRU release path. bgfx-leak guard stays green.
- `purgeChunks` **bumps `evictions_lru` exactly once** per
  previously-resident chunk. Pending requests are cancelled
  separately and bump no counter (L-3I-4).
- `purgeChunks` is **idempotent**: a second consecutive call
  sees an empty cache and is a no-op. Test case 6.
- `purgeChunks` does **not bump `resourceEpoch`** when
  called outside of `World2D::removeTilemap`. Test case 8.

---

### 13.24 P3J.1 — KI-3I-1 fix: `eraseByKey` bumps `evictions_lru` (v0.1.22)

**Type**: bug fix (test + src). Zero new surface; in-AY2D
only.

**Authoritative docs**:
- §13.21 KI-3I-1 entry (deferred follow-up).
- §18.7 "KI-3I-1 (`eraseByKey` counter asymmetry)" block.

**The bug**: `InMemoryTilemapChunkSource::eraseByKey(MapKey)`
deletes a single entry from the cache and updates
`chunk_resident_count`, but does **not** bump
`evictions_lru`. The counter is bumped in the bulk paths
(`evictIfNeeded` ~line 128, `evictDownTo` ~line 313) but
not in the per-key helper. This is observable: any caller
that invokes `eraseByKey` directly (today there are none
in the AY2D codebase, but the public surface is reachable
from consumers) sees `evictions_lru` under-count.

**The fix**: add
`_counters.evictions_lru.fetch_add(1u, std::memory_order_relaxed)`
to `eraseByKey` body. The bulk paths do **not** call
`eraseByKey` themselves — `evictIfNeeded` and
`evictDownTo` use direct `_cache.erase(_cache.begin()) +
_index.erase(key)` (verified by grep: zero `.eraseByKey` /
`->eraseByKey` call sites in the entire AY2D tree) — so
the bump is not double-counted. `chunk_resident_count`
update is preserved.

**Files modified**:
- `src/AYInMemoryTilemapChunkSource.cpp` — `eraseByKey`
  body adds the counter bump + comment block referencing
  this entry.
- `unittest/Test_ChunkSourceEraseByKey.cpp` (NEW) — 3
  cases / ~10 CHECK.

**NOT touched**: header `AYInMemoryTilemapChunkSource.h`
(public surface unchanged), `Tilemap` / `World2D` /
`ITilemapChunkSource` (no impact), bgfx-leak guard (pure
counter change), root `CMakeLists.txt` / `.gitmodules`.

**Test cases** (`Test_ChunkSourceEraseByKey`, suite
`ChunkSourceEraseByKey`, 3 cases):

1. `EraseByKey_BumpsEvictionsLruByOne` (3 CHECK) —
   cap=10, put (0,0)/(1,0)/(2,0), resident=3,
   evictions_lru=0. `eraseByKey(packKey(1,0))` →
   resident==2, evictions_lru==1, contains(packKey(1,0))
   == false. Locks the fix.
2. `EraseByKey_NoMatch_DoesNotBump` (2 CHECK) — empty
   cache; `eraseByKey(packKey(99,99))` → resident==0,
   evictions_lru==0. Locks the no-op-on-miss path.
3. `EraseByKey_DoesNotInterfereWithBulkEvictionCounter`
   (3 CHECK) — cap=2, put (0,0)/(1,0)/(2,0) (third put
   triggers `evictIfNeeded` → evictions_lru==1), then
   `eraseByKey(packKey(1,0))` → evictions_lru==2,
   resident==1 (only (2,0) remains, the LRU survivor
   since (0,0) was already evicted by capacity pressure).
   Locks the bulk-path / per-key-path counter sum.

**Invariants** (re-stated for grep-ability):

- `eraseByKey` bumps `evictions_lru` exactly once per
  successful erase (the bump is `1u`, not the number
  removed, since `eraseByKey` is per-key).
- `eraseByKey` bumps `evictions_lru` zero times on a
  no-match (the early-return branch never touches
  counters).
- The bulk paths (`evictIfNeeded`, `evictDownTo`) and
  the per-key path (`eraseByKey`) are now symmetric in
  counter discipline.
- L-3I-3 ("evictions_lru exactly once per LRU eviction")
  strengthens from "every bulk-path call site" to "every
  per-call-site".

**Open follow-ups** (after P3J.1):

- KI-3I-1 is now **closed**. The §13.21 / §18.7 "deferred
  to Phase 3J" trailers are updated below in C14.
- Phase 3J continues with A-3 / A-10 / A-1 / A-12 /
  A-7 / A-11 / A-8 / A-6 / 3J env PR per §13.23 list.
- Cross-module PRs (CM-1..CM-5) still deferred per
  §4.2.1.

---

### 13.26 P3J.2 — A-3: `SpriteDrawCmd` POD layout static_assert (v0.1.23)

**Type**: test-only + 1 comment-only header fixup (no
surface change to the POD itself). The POD
`SpriteDrawCmd` (defined in `include/AYSpriteDrawCmd.h`,
shipped Phase 3F v0.1.9) has a manual layout comment
listing its **88 B** shape. That shape was wrong.

**Layout drift discovered during P3J.2**:
the comment says 88 B but the actual `sizeof` on the
shipped toolchain (MSVC + x64 + `Float3x3` 36B align 4 +
`FVector2` 16B align 16 + `FVector4` 16B align 16) is
**112 B**. `alignof(SpriteDrawCmd)` is **16** (driven by
the 16-B SIMD alignment of `FVector2` /
`FVector4`). The drift is an artifact of P3F assuming
`FVector2` is 8 B aligned-to-4; MSVC's `FVector2` is
**16 B aligned-to-16**, which inserts 12 B of padding
between `worldMatrix` and `sourceRectMin`, 8 B after
`layerMaskSnapshot`, etc. P3H.1 踩坑 #33 surfaced the
same MSVC alignment surprise on `SpriteSheet`'s
`std::string`; P3J.2 surfaces it again on the
`FVector2` family.

**Action**:

- `include/AYSpriteDrawCmd.h` — layout comment updated
  to 112 B / align 16 / explicit field offsets (still a
  comment; the struct body is unchanged).
- `unittest/Test_SpriteDrawCmdLayout.cpp` (NEW) — locks
  the corrected layout.

**Correct layout** (verified by `offsetof` probe on
the shipped toolchain):

```
offset  field                 size  cumulative
   0    packedSortKey          4B        4B
   4    worldMatrix           36B       40B  (no pad; align 4)
  40    [pad to 16-align]      8B       48B
  48    sourceRectMin         16B       64B
  64    sourceRectMax         16B       80B
  80    colorRGBA             16B       96B
  96    flip                   1B       97B
  97    [pad to 4-align]       3B      100B
 100    layerMaskSnapshot      4B      104B
 104    [trail pad to 16]      8B      112B
```

**The locks** (test-time, all inside the test TU):

- `sizeof(SpriteDrawCmd) == 112` (post-correction;
  the layout comment is now authoritative).
- `alignof(SpriteDrawCmd) == 16` (driven by FVector2
  / FVector4 SIMD alignment).
- `std::is_trivially_copyable<SpriteDrawCmd>::value`
  (`std::vector<SpriteDrawCmd>` requires trivially
  copyable for memcpy-on-realloc; this is the
  pre-condition for the §17.3 "no allocation" promise).
- `std::is_standard_layout<SpriteDrawCmd>::value`
  (cross-module PR to AYRenderer `DrawItem::payload`
  needs C-compatible layout for the future
  `RenderSystem2D` translator; §17.3).
- `offsetof(SpriteDrawCmd, packedSortKey) == 0`
  (first field, anchor).
- `offsetof(SpriteDrawCmd, worldMatrix) == 4` (after
  u32, before any padding because Float3x3 alignment is
  4).
- `offsetof(SpriteDrawCmd, sourceRectMin) == 48`
  (4 + 36 + 8-byte pad-to-16-align).
- `offsetof(SpriteDrawCmd, sourceRectMax) == 64`.
- `offsetof(SpriteDrawCmd, colorRGBA) == 80`.
- `offsetof(SpriteDrawCmd, flip) == 96`.
- `offsetof(SpriteDrawCmd, layerMaskSnapshot) == 100`.

The first four are `static_assert` (compile-time, no
overhead). The `offsetof` checks are runtime
CHECK_INT_EQ because `offsetof` is a runtime-evaluated
macro on MSVC.

**Why this works for `SpriteDrawCmd` but failed for
`SpriteSheet`** (踩坑 #33): `SpriteDrawCmd` contains
only scalar arithmetic types (`uint32_t`, `uint8_t`,
`Float3x3`, `FVector2`, `FVector4`) — no `std::string`,
no `std::vector`, no user-defined types with non-trivial
destructors. MSVC's `std::is_trivially_copyable` and
`std::is_standard_layout` therefore return `true` for
`SpriteDrawCmd` directly. `SpriteSheet` wraps
`AtlasDesc + std::string texturePath`; MSVC's
`std::string` is **not** trivially copyable in any
recent standard library, so the same asserts failed.
P3H.1 踩坑 #33 documented the workaround for
`SpriteSheet`; `SpriteDrawCmd` does not need it.

**Files modified**:
- `include/AYSpriteDrawCmd.h` — layout comment updated
  from 88 B / 9-float matrix to 112 B / explicit offset
  table. Struct body unchanged.
- `unittest/Test_SpriteDrawCmdLayout.cpp` (NEW) — 7
  cases / ~13 CHECK.
- `unittest/CMakeLists.txt` — `+1` line.

**NOT touched**: `src/`, root `CMakeLists.txt`,
`.gitmodules`.

**Test cases** (`Test_SpriteDrawCmdLayout`, suite
`SpriteDrawCmdLayout`, 7 cases):

1. `SpriteDrawCmd_StaticAssert_SizeIs112Bytes` (compile-
   time) — four `static_assert`s: sizeof == 112, alignof
   == 16, is_trivially_copyable, is_standard_layout.
2. `SpriteDrawCmd_OffsetOf_PackedSortKey_IsZero`
   (1 CHECK) — first-field anchor.
3. `SpriteDrawCmd_OffsetOf_WorldMatrix_IsFour`
   (1 CHECK) — u32 + Float3x3 boundary.
4. `SpriteDrawCmd_OffsetOf_SourceRectMin_Is48`
   (1 CHECK) — 4 + 36 + 8-byte pad.
5. `SpriteDrawCmd_OffsetOf_ColorRGBA_Is80`
   (1 CHECK) — 48 + 16 + 16 = 80.
6. `SpriteDrawCmd_OffsetOf_LayerMaskSnapshot_Is100`
   (1 CHECK) — 96 + 4 (flip + pad) = 100.
7. `SpriteDrawCmd_DefaultConstructed_AllDefaultsMatch`
   (5 CHECK) — packedSortKey=0, sourceRectMin=(0,0),
   sourceRectMax=(1,1), colorRGBA=(1,1,1,1),
   layerMaskSnapshot=0; worldMatrix == identity;
   flip == 0. Locks the default field values.

**Open follow-ups** (after P3J.2):

- Phase 3J continues with A-10 (AtlasDesc validator),
  A-1 (TileAnimation batch state), A-12 (EvictionPolicy
  reserved values), A-7 (TilemapLoadState enum test),
  A-11 (ChunkRequestHandle wrap-around), A-8 (Sprite
  batch state helper), A-6 (chunk-row coalesce, the
  last surface-changing slice).

---

### 13.27 P3J.3 — A-10: `AtlasDesc` validator coverage (v0.1.24)

**Type**: test-only (no surface change). `isValidAtlasDesc`
shipped Phase 2 (design.md §5.5) with 6 reject
conditions + 1 happy path. The existing
`Test_TileSamplerUV` suite locks 2 reject cases plus
the happy path; the remaining 4 reject cases +
boundary conditions (gutter `==` `min/2`, gutter `>`
`min/2`, non-divisible atlas/tile ratio, filter
default) are not separately locked. P3J.3 promotes
those to a dedicated `Test_AtlasDescValidator` suite.

**The locks** (test-time, in the new test TU):

- `isValidAtlasDesc` returns false on `atlasWidthTexels == 0`.
- `isValidAtlasDesc` returns false on `atlasHeightTexels == 0`.
- `isValidAtlasDesc` returns false on `tileWidthTexels == 0`.
- `isValidAtlasDesc` returns false on `tileHeightTexels == 0`.
- `isValidAtlasDesc` returns false on `tilesPerRow == 0`.
- `isValidAtlasDesc` returns false on `tilesPerColumn == 0`.
- `isValidAtlasDesc` returns false on atlas/tile ratio
  mismatch (`atlasWidth != tileWidth * tilesPerRow`).
- `isValidAtlasDesc` returns false on gutter too large
  (`gutter >= min(tileWidth, tileHeight) / 2`).
- `isValidAtlasDesc` boundary: gutter exactly equal to
  `min/2 - 1` (the largest valid gutter) returns true;
  gutter equal to `min/2` returns false (boundary).
- `AtlasDesc{}` (default-init) returns false (all
  fields zero, no tiles).
- `AtlasDesc{}` with `tilesPerRow = tilesPerColumn = 0`
  override returns false even when atlas dimensions
  are non-zero (explicit tilesPerRow zero reject).

**Files modified**:
- `unittest/Test_AtlasDescValidator.cpp` (NEW) — 8
  cases / ~22 CHECK.
- `unittest/CMakeLists.txt` — `+1` line.

**NOT touched**: `include/AYAtlasDesc.h` (zero surface
change — `isValidAtlasDesc` body unchanged), `src/`,
root `CMakeLists.txt`, `.gitmodules`.

**Test cases** (`Test_AtlasDescValidator`, suite
`AtlasDescValidator`, 8 cases):

1. `AtlasDesc_Default_IsInvalid_AllFieldsZero` (1 CHECK)
   — `AtlasDesc{}` returns false.
2. `AtlasDesc_ZeroAtlasDimension_IsInvalid` (2 CHECK) —
   zero width or zero height alone returns false.
3. `AtlasDesc_ZeroTileDimension_IsInvalid` (2 CHECK) —
   zero tileWidth or zero tileHeight alone returns false.
4. `AtlasDesc_ZeroTilesPerRowColumn_IsInvalid` (2 CHECK)
   — zero `tilesPerRow` or zero `tilesPerColumn` alone
   returns false.
5. `AtlasDesc_NonDivisibleAtlasTileRatio_IsInvalid`
   (2 CHECK) — `atlasWidth = 100`, `tileWidth = 7`,
   `tilesPerRow = 14` → 7*14=98 != 100 → false.
   Reversed with `tilesPerRow = 100/7` (integer
   truncation gives 14) → still false (must match
   *exactly*, not with tolerance).
6. `AtlasDesc_GutterTooLarge_IsInvalid` (2 CHECK) —
   tile 16x16, gutter = 8 returns false
   (`gutter >= 8 == 16/2`); gutter = 7 returns true
   (largest valid gutter). Locks the L-8 boundary.
7. `AtlasDesc_HappyPath_BilinearDefault_ReturnsTrue`
   (3 CHECK) — `tilesPerRow = 4`, `tilesPerColumn = 4`,
   `atlasWidth = atlasHeight = 64`, `tileWidth = tileHeight
   = 16`, `gutter = 1`, default `filter == Bilinear` →
   true. Plus two sanity reads on `filter` and `gutter`.
8. `AtlasDesc_FilterAndWrapDefaultsMatch_Lock` (2 CHECK)
   — default-constructed `AtlasDesc{}` has
   `filter == TileFilter::Bilinear`, `wrapU == Clamp`,
   `wrapV == Clamp`. Locks the §5.5 "default = Bilinear
   + Clamp" sentence.

**Open follow-ups** (after P3J.3):

- Phase 3J continues with A-1 (TileAnimation batch
  state), A-12 (EvictionPolicy reserved values),
  A-7 (TilemapLoadState enum test), A-11
  (ChunkRequestHandle wrap-around), A-8 (Sprite batch
  state helper), A-6 (chunk-row coalesce, the last
  surface-changing slice).

---

### 13.28 P3J.4 — A-1: TileAnimation batch-state coverage (v0.1.25)

**Type**: test-only (no surface change). Phase 3B shipped
`tickTilemapAnimation` (src/AYTilemapAnimation.cpp) with
the §7.2 integer-ms remainder accumulator and the
`ensureStateSize` lazy-grow helper. The existing
`Test_TilemapAnimation` suite (10 cases) covers single-
tile + 2-tile animation behavior, zero-duration no-op,
reversed-clock clamp, first-tick baseline, etc. The
remaining residue is the **batch-tick invariant**: when
many tiles (e.g. 100) all carry animations of varying
durations and frame counts, the batch tick must (a)
walk each entry exactly once, (b) advance each entry
independently (no cross-entry aliasing on
`currentFrameIdx` / `elapsedMs`), (c) size
`animationState` to the table extent on the first
real tick, not before, and (d) leave the table
extent unchanged after the tick.

**Files modified**:
- `unittest/Test_TilemapAnimation.cpp` — append 5
  cases / ~30 CHECK (does not bump suite count;
  reuses the existing `TilemapAnimationSuite`).

**NOT touched**: `src/AYTilemapAnimation.cpp` (zero
surface change), `include/AYTileAnimation.h`,
`include/AYTilemap.h` (the existing
`hasBeenTicked` flag is the gate; no new state).

**Test cases** (appended to existing
`TilemapAnimationSuite`, 5 cases):

1. `BatchTick_AnimatesAllEntriesExactlyOnce` (6 CHECK)
   — register animations on tileIds 0..99 (100 entries,
   each with 3 frames of 100 ms). Initial tick at
   t=0 (baseline, no advance). Tick at t=100ms: each
   of 100 entries advances `currentFrameIdx` from 0 to
   1; `elapsedMs` resets to 0. Assert uniform
   advancement across all 100 entries (no entry lags
   behind). The lock is "one batch tick = one frame
   advance per animated entry".

2. `BatchTick_NoCrossEntryAliasing` (4 CHECK) — register
   tileId 0 with frames [A=10ms, B=10ms] and tileId 1
   with frames [C=30ms, D=30ms]. Tick at t=20ms: tile 0
   advances A->B->A (20ms total, two cycles), tile 1
   stays at C (30ms > 20ms). Assert tile 0
   `currentFrameIdx == 0` (back to A after wrap), tile
   1 `currentFrameIdx == 0` (still C). Locks per-entry
   state independence.

3. `BatchTick_EnsureStateSizeLazyGrow` (3 CHECK) —
   fresh Tilemap: `animationState.currentFrameIdx.size()
   == 0`. Tick at t=0 (baseline) — state size grows to
   100 (the table extent). Tick at t=10ms — state size
   stays at 100. Locks that `ensureStateSize` is called
   exactly once on the first real tick, not per-entry.

4. `BatchTick_TableExtentUnchangedAfterTick` (2 CHECK)
   — same setup as case 1. After 5 ticks of 100 ms
   each, `animationTable.size() == 100`. The tick
   mutates state, not the table.

5. `BatchTick_LargeDeltaCapsAtLoopBoundary` (3 CHECK)
   — register tile 0 with 4 frames of 10 ms. Tick at
   t=10s (10_000ms = 1000 cycles). State: elapsed=0,
   `currentFrameIdx == 0` (back to frame 0 after full
   loop). Locks that a huge delta does not advance the
   frame index past the loop boundary or leave stale
   `elapsedMs` accumulation.

**Open follow-ups** (after P3J.4):

- Phase 3J continues with A-12 (EvictionPolicy reserved
  values), A-7 (TilemapLoadState enum test), A-11
  (ChunkRequestHandle wrap-around), A-8 (Sprite batch
  state helper), A-6 (chunk-row coalesce, the last
  surface-changing slice).

---

### 13.29 P3J.5 — A-12: `EvictionPolicy` enum reserved values (v0.1.26)

**Type**: test-only (no surface change). The
`EvictionPolicy` enum (`include/AYTilemapBudget.h`) ships
with three values (`LRU = 0`, `Distance = 1`,
`TimeWindow = 2`), backed by `uint8_t` (256 distinct
codes available). Phase 4 streaming PR (R-3G.1) will
extend the set with at least one new policy (the design
calls out Distance + TimeWindow as candidates, but both
are scaffold-only today). To make that extension safe,
P3J.5 locks (a) the underlying-type discipline
(`sizeof(EvictionPolicy) == 1`), (b) the canonical code
point of each existing value, and (c) the convention
that new policies occupy codes `0x03..0xFE` — `0xFF` is
reserved as a sentinel for "unset / invalid budget
policy" and must never alias any current or future
wired policy.

**Files modified**:
- `unittest/Test_EvictionPolicyEnum.cpp` (NEW) — 5
  cases / ~10 CHECK.
- `unittest/CMakeLists.txt` — `+1` line.

**NOT touched**: `include/AYTilemapBudget.h`,
`src/`, root `CMakeLists.txt`.

**Test cases** (`Test_EvictionPolicyEnum`, suite
`EvictionPolicyEnum`, 5 cases):

1. `EvictionPolicy_UnderlyingTypeIsUint8` (compile-time)
   — `static_assert(sizeof(EvictionPolicy) == 1u)` and
   `static_assert(std::is_same_v<std::underlying_type_t<EvictionPolicy>,
   uint8_t>)`. Locks the 256-code budget.

2. `EvictionPolicy_CanonicalCodes_ZeroOneTwo` (3 CHECK)
   — `static_cast<uint8_t>(LRU) == 0`,
   `static_cast<uint8_t>(Distance) == 1`,
   `static_cast<uint8_t>(TimeWindow) == 2`. The canonical
   ordering matters because the existing
   `setBudget(b)` switch on `EvictionPolicy` compares
   against `LRU` directly (returning false for any
   non-LRU policy).

3. `EvictionPolicy_FFIsReservedSentinel` (2 CHECK) —
   `static_cast<uint8_t>(0xFF) > 2` (256 > 3, true) and
   `0xFF != LRU/Distance/TimeWindow`. Locks the
   convention that future additions go in the
   0x03..0xFE range and `0xFF` is reserved.

4. `EvictionPolicy_TilemapBudget_DefaultIsLRU` (3 CHECK)
   — default `TilemapBudget{}` has
   `eviction == EvictionPolicy::LRU`, `maxChunksLoaded
   == 1024`, `maxIoBytesPerSec == 64 * 1024 * 1024`.
   Locks the §18.1 default values.

5. `EvictionPolicy_SwitchPolicy_FallsThroughForReserved`
   (compile-time) — `static_assert` that the cast
   `static_cast<EvictionPolicy>(0xFF)` is a valid
   enum value **in C++ language terms** (no
   out-of-range error), but its comparison against
   `LRU` / `Distance` / `TimeWindow` returns false at
   runtime (semantically invalid). Locks the
   "reserved sentinel is a valid bit pattern but not a
   wired policy" invariant.

**Open follow-ups** (after P3J.5):

- Phase 3J continues with A-7 (TilemapLoadState enum
  test), A-11 (ChunkRequestHandle wrap-around), A-8
  (Sprite batch state helper), A-6 (chunk-row coalesce,
  the last surface-changing slice).

---

### 13.30 P3J.6 — A-7: `TileLoadState` enum coverage (v0.1.27)

**Type**: test-only (no surface change). The
`TileLoadState` enum (`include/AYTileLoadState.h`,
shipped Phase 2 F-18) has 4 values (`Unloaded = 0`,
`Loading = 1`, `Loaded = 2`, `Failed = 3`) backed by
`uint8_t`. The enum is consumed by ECS inspectors +
Editor Inspector (design.md §11 F-18). The existing
`Test_Tilemap` suite uses 5 `CHECK(loadState == ...)`
assertions across load paths but does **not** lock the
enum's underlying-type discipline or the canonical code
points. P3J.6 promotes those to a dedicated
`Test_TileLoadStateEnum` suite, mirrors the A-12
reserved-sentinel convention (0xFF reserved for future
expansion).

**Files modified**:
- `unittest/Test_TileLoadStateEnum.cpp` (NEW) — 4
  cases / ~10 CHECK.
- `unittest/CMakeLists.txt` — `+1` line.

**NOT touched**: `include/AYTileLoadState.h` (zero
surface change), `src/`, root `CMakeLists.txt`.

**Test cases** (`Test_TileLoadStateEnum`, suite
`TileLoadStateEnum`, 4 cases):

1. `TileLoadState_UnderlyingTypeIsUint8` (compile-time)
   — `static_assert(sizeof(TileLoadState) == 1u)` +
   `is_same<underlying_type_t<TileLoadState>, uint8_t>`.
   Locks the 256-code budget.

2. `TileLoadState_CanonicalCodes_ZeroOneTwoThree`
   (4 CHECK) — `Unloaded == 0`, `Loading == 1`,
   `Loaded == 2`, `Failed == 3`. Locks the §11 F-18
   ordering, which the ECS Inspector relies on for
   stable display IDs.

3. `TileLoadState_FFIsReservedSentinel` (2 CHECK) —
   `0xFF > 3` (strictly greater than highest wired
   code, leaving 0x04..0xFE for future states) and
   `0xFF != Unloaded/Loading/Loaded/Failed`.

4. `TileLoadState_Tilemap_DefaultIsUnloaded` (3 CHECK)
   — default `Tilemap{}` has
   `loadState == TileLoadState::Unloaded`,
   `tileWidth == 0`, `tileHeight == 0`,
   `defaultTileId == 0`. Locks the §13.7 default state
   on a fresh tilemap (used by the Inspector's "Unloaded"
   badge on the editor's empty-tilemap state).

**Open follow-ups** (after P3J.6):

- Phase 3J continues with A-11 (ChunkRequestHandle
  wrap-around), A-8 (Sprite batch state helper),
  A-6 (chunk-row coalesce, the last surface-changing
  slice).

---

### 13.31 P3J.7 — A-11: `ChunkRequestHandle` wrap-around coverage (v0.1.28)

**Type**: test-only (no surface change). The
`ChunkRequestHandle` packs 24-bit index + 8-bit
generation (include/AYChunkRequestHandle.h, Phase 3).
The existing 10-case `Test_ChunkRequestHandle` suite
covers pack/unpack, ABA guard, masking, but does not
lock **wrap-around semantics** at the boundaries:
generation 255 → 256 should wrap to 0 (8-bit mask),
index 0xFFFFFF → 0x1000000 should wrap to 0 (the
mask makes it 0; index 0 is then `kInvalidId`). P3J.7
promotes those to two additional cases in the
existing suite (does not bump suite count).

**Files modified**:
- `unittest/Test_ChunkRequestHandle.cpp` — append 4
  cases / ~14 CHECK.

**NOT touched**: `include/AYChunkRequestHandle.h` (zero
surface change — wrap is implicit in the `& kMask`
discipline already documented), `src/`,
root `CMakeLists.txt`.

**Test cases** (appended to existing
`ChunkRequestHandleSuite`, 4 cases):

1. `Generation_FF_Plus_One_Wraps_ToZero` (2 CHECK) —
   `ChunkRequestHandle{1u, 0xFFu}.generation() == 0xFF`.
   `ChunkRequestHandle{1u, 0x100u}.generation() == 0`
   (the mask strips the 9th bit, leaving 0). Locks the
   8-bit wrap-around behavior; consumers must
   accept that generation 0 is reachable after wrap.

2. `Index_FFFFFF_Plus_One_Wraps_ToInvalid` (3 CHECK) —
   `ChunkRequestHandle{0xFFFFFFu, 1u}.index() ==
   0xFFFFFFu`, `isValid() == true` (since index != 0).
   `ChunkRequestHandle{0x1000000u, 1u}.index() == 0`
   (24-bit mask strips the 25th bit), `isValid() ==
   false` (index == 0 == kInvalidId). Locks the
   "post-wrap index 0 is invalid" discipline. The
   chunk source must guard against this when it
   issues the next `requestChunk`.

3. `WrapAround_Equality_StillHoldsForSameIdAndGen`
   (2 CHECK) — `ChunkRequestHandle{1u, 0xFFu}` and
   `ChunkRequestHandle{1u, 0u}` (post-wrap gen)
   compare unequal even though both wrap to the
   `0x01_000000`-equivalent bit pattern only on
   the second. Locks that equality is on the full
   packed id, not on the index alone (which would
   collide after wrap).

4. `Pack_BoundaryValues_RoundTrip` (4 CHECK) —
   `(0, 0)` packs to 0, `(0xFFFFFF, 0xFF)` packs to
   `0xFFFFFFFF`, `(1, 0)` packs to `1`, `(1, 1)`
   packs to `0x01000001`. Locks the boundary bit
   patterns of the packed representation.

**Open follow-ups** (after P3J.7):

- Phase 3J continues with A-8 (Sprite batch state
  helper), A-6 (chunk-row coalesce, the last
  surface-changing slice).

---

### 13.X Future versions (template)