// AY2DEngineDemo.cpp — CM-4 (2026-08-11): standalone 2D demo exe.
//
// Two integration paths in one window (key 1 / key 2 to switch):
//   1 = ECS main line  — AYEntity 2D components + 刀 2 systems:
//       Transform + TilemapComponent + SpriteComponent +
//       OrthoCameraComponent entities; bootstrapModule() wires
//       OrthoCameraUpdateSystem(405) + TilemapRenderSystem(510) +
//       SpriteRenderSystem(510), which drive RendererSubSystem's
//       scene-builder chain + setMainCamera (2D ortho).
//   2 = direct AY2D line — ayt::ay2d::World2D::addTilemap +
//       std::vector<ayt::ay2d::Sprite> -> buildSpriteScene ->
//       SpriteDrawCmd -> DrawItem translation, tile draws via the
//       REAL ayt::ay2d::tileUV / cellToWorld, camera via the REAL
//       ayt::ay2d::OrthographicCamera. The demo is the only layer
//       allowed to hold AY2D data and translate it into DrawItem
//       (it links both AY2D and AYRenderer).
//
// Acceptance: run 120 frames, screenshots at frame 30/60 must be
// non-black (tilemap grid + sprite overlay + panning camera).
// Esc quits early.

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
#include "AYGameLoop.h"
#include "AYRenderer/RendererSubSystem.h"
#include "AYRenderer/TilemapShaderSources.h"
#include "AYEntity/World.h"

#include "AYEntity/components/OrthoCameraComponent.h"
#include "AYEntity/components/SpriteComponent.h"
#include "AYEntity/components/TilemapComponent.h"
#include "AYEntity/components/TransformComponent.h"

#include "AY2D/AtlasDesc.h"
#include "AY2D/OrthographicCamera.h"
#include "AY2D/Sprite.h"
#include "AY2D/SpriteCulling.h"
#include "AY2D/SpriteDrawCmd.h"
#include "AY2D/TileMath.h"
#include "AY2D/TileSamplerUV.h"
#include "AY2D/World2D.h"

#include <AYResource/assetsDefs/ITilemap.h>
#include <AYResource/ResourceManager.h>
#include "AYResource/assetsImpl/TilemapAsset.h"
#include "AYResource/assetsImpl/Texture.h"

#include "AYIO/File.h"
#include <AYMath/MathTransform.h>
#include <AYMath/MathTypes.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef AY_SHADER_SHADERC_HINT
#  define AY_SHADER_SHADERC_HINT ""
#endif

namespace {

// ---- World constants ------------------------------------------------------
constexpr int    kWindowWidth   = 1280;
constexpr int    kWindowHeight  = 720;
constexpr int    kShotFrames[]  = {30, 60};
constexpr int    kShotCount     = 2;

constexpr uint32_t kMapCols       = 20;
constexpr uint32_t kMapRows       = 15;
constexpr uint32_t kTilePx        = 16;
constexpr uint32_t kAtlasTilesPerRow    = 4;
constexpr uint32_t kAtlasTilesPerColumn = 4;
constexpr uint32_t kAtlasTexels         = kTilePx * kAtlasTilesPerRow;  // 64

constexpr float kMapCenterX = kMapCols * kTilePx * 0.5f;   // 160
constexpr float kMapCenterY = kMapRows * kTilePx * 0.5f;   // 120
constexpr float kCameraZoom = 2.0f;
constexpr float kMinZoom     = 0.5f;   // wheel-zoom clamp
constexpr float kMaxZoom     = 8.0f;
constexpr float kViewSize   = 540.0f;  // vertical world extent at zoom 1

constexpr int kSpriteCount = 12;

// Dense-atlas palette, one color per tile id (0..15). Atlas row 0 =
// bottom row (origin-bottom-left convention, AY2D/TileSamplerUV.h).
constexpr uint8_t kPalette[16][3] = {
    { 95,  95, 105},  //  0 floor (gray)
    { 55, 115,  55},  //  1 grass border
    { 65, 155, 205},  //  2 water pond
    {115,  75, 205},  //  3
    {205, 160,  70},  //  4 sand patch
    {160,  70,  70},  //  5
    { 70, 180, 120},  //  6
    {200,  90, 140},  //  7
    {140, 140, 200},  //  8
    {215, 125,  60},  //  9 hazard row (orange)
    {100, 205, 205},  // 10
    {170, 205,  90},  // 11
    { 90, 130, 225},  // 12
    {225,  80,  80},  // 13
    {130,  90,  60},  // 14
    {235, 205, 125},  // 15
};

// Deterministic tile pattern for the 20x15 ground map.
uint32_t tileFor(uint32_t col, uint32_t row)
{
    if (col == 0 || row == 0 || col == kMapCols - 1 || row == kMapRows - 1) {
        return 1;  // grass border
    }
    if (row == 3 && (col % 4) == 2) {
        return 2;  // water ponds
    }
    if (row == 11 && (col % 5) == 1) {
        return 9;  // hazard row
    }
    if ((col * 7 + row * 3) % 11 == 0) {
        return 4;  // sand patches
    }
    return 0;  // floor
}

// ---- helpers ---------------------------------------------------------------

bool fileExists(const std::string& path)
{
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

bool writeBytes(const std::string& path, const void* data, size_t size)
{
    ayt::io::File file(path, ayt::io::File::Mode::BinaryWrite);
    if (!file.isOpen()) {
        return false;
    }
    return file.write(data, size) == size;
}

bool ensureAssetDirectory(const std::string& path)
{
#ifdef _WIN32
    if (CreateDirectoryA(path.c_str(), nullptr) != 0) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
#else
    (void)path;
    return true;
#endif
}

// Float3x3 (row-major affine, AY2D/SpriteDrawCmd.h) -> Float4x4 (DrawItem
// world). Rows 0..2 copy through, row 3 = (0,0,0,1).
ayt::math::Float4x4 to4x4(const ayt::math::Float3x3& m)
{
    ayt::math::Float4x4 out = ayt::math::Float4x4::identity();
    out(0, 0) = m(0, 0); out(0, 1) = m(0, 1); out(0, 2) = m(0, 2);
    out(1, 0) = m(1, 0); out(1, 1) = m(1, 1); out(1, 2) = m(1, 2);
    out(2, 0) = m(2, 0); out(2, 1) = m(2, 1); out(2, 2) = m(2, 2);
    return out;
}

// Affine world matrix for a 2D sprite: T * R(z) * S, row-major.
ayt::math::Float3x3 makeAffine(float px, float py, float rot,
                               float sx, float sy)
{
    const float c = std::cos(rot);
    const float s = std::sin(rot);
    return ayt::math::Float3x3(
        sx * c, -s * sy, px,
        sx * s,  c * sy, py,
        0.0f,   0.0f,   1.0f);
}

// Mirror of TilemapRenderSystem's anonymous-ns helper: tile id at
// (col,row), defaultTileId when out of range / empty.
uint32_t tileIdAt(const ayt::resource::ITilemap& map, uint32_t col, uint32_t row)
{
    const uint32_t cols = map.getCols();
    if (cols == 0 || row >= map.getRows() || col >= cols) {
        return map.getDefaultTileId();
    }
    const uint32_t idx = row * cols + col;
    if (idx >= map.getTileIdCount()) {
        return map.getDefaultTileId();
    }
    if (map.getPackMode() == ayt::resource::TilemapPackMode::Narrow16) {
        const uint16_t* ids = map.getTileIds16();
        return ids ? static_cast<uint32_t>(ids[idx]) : map.getDefaultTileId();
    }
    const uint32_t* ids = map.getTileIds32();
    return ids ? ids[idx] : map.getDefaultTileId();
}

// ---- demo state ------------------------------------------------------------

enum class DemoPath { Ecs = 1, Direct2D = 2 };

struct Direct2DPath {
    ayt::ay2d::World2D        world2d;
    ayt::ay2d::TilemapHandle  tmHandle;             // invalid until addTilemap
    std::vector<ayt::ay2d::Sprite> sprites;
    ayt::ay2d::OrthographicCamera camera;
    std::shared_ptr<ayt::resource::ITilemap> tilemap;   // loaded .aytilemap
    ayt::render::MeshHandle      quad;                  // unit quad (created once)
    ayt::render::TextureHandle   atlasTex;
    ayt::render::TextureHandle   spriteTex;
    ayt::render::MaterialHandle  material;              // kTilemapPhoskiaSource, albedoMap = atlas
    ayt::render::MaterialHandle  spriteMaterial;        // kTilemapPhoskiaSource, albedoMap = spriteTex
    bool ready = false;     // CPU-side (World2D + sprites + tilemap) done
    bool gpuReady = false;  // GPU-side (quad/tex/material) lazily created
                            // on first builder call — the renderer is not
                            // initialized until the first loop tick, so
                            // create* calls in spawnDirect2DPath would
                            // all fail (renderer init happens in the
                            // RendererSubSystem onStart on the first
                            // world update).
};

struct DemoState {
    ayt::game::GameLoop* loop = nullptr;
    bool                   running = true;
    int                    frame = 0;
    int                    frameCap = 0;  // 0 = run until Esc / window close
    DemoPath               path = DemoPath::Ecs;
    bool                   key1Down = false;  // edge-detect latches
    bool                   key2Down = false;

    // Interactive camera (wheel zoom + left-drag pan).
    float                  zoom = kCameraZoom;  // clamped [kMinZoom, kMaxZoom]
    bool                   autoPan = true;      // orbit until first drag
    bool                   dragging = false;    // left button down
    POINT                  dragLastCursor{};
    float                  dragCamX = 0.0f, dragCamY = 0.0f;  // drag-maintained cam pos

    std::string assetDir;
    std::string tilemapPath;
    std::string atlasPath;
    std::string spritePath;
    std::string screenshotDir;

    // ECS line entities (destroyed on switch to direct line).
    std::vector<ayt::entity::Entity*> ecsEntities;
    ayt::entity::Entity*              ecsCamera = nullptr;
    ayt::entity::OrthoCameraComponent* ecsCameraComp = nullptr;

    Direct2DPath direct;
    std::vector<ayt::render::DrawPayload2D> payloads;  // borrow buffer for direct line
};

// ---- bake -------------------------------------------------------------------

// 64x64 RGBA8 dense atlas: 4x4 grid of 16px tiles, one palette color
// per tile id (id 0 at bottom-left).
bool bakeAtlas(const std::string& path)
{
    constexpr uint32_t size = kAtlasTexels * kAtlasTexels * 4u;  // 16384

    ayt::resource::Texture atlas;
    atlas._width  = kAtlasTexels;
    atlas._height = kAtlasTexels;
    atlas._format = ayt::resource::TextureFormat::RGBA8;
    ayt::resource::UInt8* px = atlas.mutableImageData(size);
    if (px == nullptr) {
        return false;
    }
    for (uint32_t y = 0; y < kAtlasTexels; ++y) {
        for (uint32_t x = 0; x < kAtlasTexels; ++x) {
            // bottom-up: tile id row = y / tilePx (row 0 = bottom).
            const uint32_t col = x / kTilePx;
            const uint32_t row = y / kTilePx;
            const uint32_t id  = row * kAtlasTilesPerRow + col;
            const uint8_t* rgb = kPalette[id % 16];
            const size_t off = (y * kAtlasTexels + x) * 4u;
            px[off + 0] = rgb[0];
            px[off + 1] = rgb[1];
            px[off + 2] = rgb[2];
            px[off + 3] = 255;
        }
    }
    std::vector<ayt::resource::UInt32> offsets = {0u};
    std::vector<ayt::resource::UInt32> sizes   = {size};
    atlas.setMipmapTable(offsets.data(), sizes.data(), 1);

    std::vector<ayt::resource::UInt8> binary;
    if (!atlas.saveToBinary(binary)) {
        return false;
    }
    return writeBytes(path, binary.data(), binary.size());
}

// 20x15 narrow16 tilemap, 16px tiles, deterministic pattern. Written to
// the makeTilemapVirtualPath spelling ("tilemaps/ground.aytilemap").
bool bakeTilemap(const std::string& path)
{
    ayt::resource::TilemapAsset asset;
    // UInt16 = ayt::math::UInt16 via the global using alias
    // (AYMath/MathDefs.h:64-65) — the same alias AYResource's own
    // AYResource/assetsImpl/TilemapAsset.h uses internally.
    asset.create(kMapCols, kMapRows,
                 static_cast<UInt16>(kTilePx),
                 static_cast<UInt16>(kTilePx),
                 ayt::resource::TilemapPackMode::Narrow16,
                 0 /*defaultTileId*/, nullptr, 0);
    for (uint32_t row = 0; row < kMapRows; ++row) {
        for (uint32_t col = 0; col < kMapCols; ++col) {
            if (!asset.setTile(row * kMapCols + col, tileFor(col, row))) {
                return false;
            }
        }
    }
    std::vector<ayt::resource::UInt8> binary;
    if (!asset.saveToBinary(binary)) {
        return false;
    }
    return writeBytes(path, binary.data(), binary.size());
}

bool bakeDemoAssets(const std::string& rootPrefix, DemoState& state)
{
    if (!ensureAssetDirectory(rootPrefix)) {
        return false;
    }
    state.assetDir       = rootPrefix + "assets\\";
    state.screenshotDir  = rootPrefix + "shots\\";
    state.tilemapPath    = state.assetDir + "tilemaps\\ground.aytilemap";
    state.atlasPath      = state.assetDir + "atlas.aytex";
    state.spritePath     = state.assetDir + "sprite.aytex";

    if (!ensureAssetDirectory(state.assetDir)
        || !ensureAssetDirectory(state.assetDir + "tilemaps\\")
        || !ensureAssetDirectory(state.screenshotDir)) {
        return false;
    }

    if (!bakeAtlas(state.atlasPath)) {
        std::fprintf(stderr, "[AY2DDemo] bakeAtlas failed\n");
        return false;
    }
    if (!bakeTilemap(state.tilemapPath)) {
        std::fprintf(stderr, "[AY2DDemo] bakeTilemap failed\n");
        return false;
    }
    ayt::resource::Texture spriteTex;
    spriteTex.createSolidColor(32, 32, 255, 200, 60, 255);
    std::vector<ayt::resource::UInt8> spriteBinary;
    if (!spriteTex.saveToBinary(spriteBinary)
        || !writeBytes(state.spritePath, spriteBinary.data(), spriteBinary.size())) {
        std::fprintf(stderr, "[AY2DDemo] bake sprite texture failed\n");
        return false;
    }

    std::fprintf(stderr, "[AY2DDemo] baked assets in %s\n", state.assetDir.c_str());
    return true;
}

// ---- ECS line ----------------------------------------------------------------

void clearEcsScene(DemoState& state)
{
    ayt::entity::World& world = ayt::entity::World::instance();
    for (ayt::entity::Entity* e : state.ecsEntities) {
        if (e != nullptr) {
            world.destroyEntity(e);
        }
    }
    state.ecsEntities.clear();
    state.ecsCamera = nullptr;
    state.ecsCameraComp = nullptr;
}

void spawnEcsScene(DemoState& state)
{
    ayt::entity::World& world = ayt::entity::World::instance();

    // Ground tilemap (needs Transform for query<Transform, TilemapComponent>).
    ayt::entity::Entity* ground = world.createEntity();
    ground->setName("ground");
    ground->addComponent<ayt::entity::Transform>();
    auto* tm = ground->addComponent<ayt::entity::TilemapComponent>();
    tm->setTilemap(state.tilemapPath.c_str());
    tm->setAtlasTexture(state.atlasPath.c_str());
    tm->atlasTilesPerRow    = static_cast<int32_t>(kAtlasTilesPerRow);
    tm->atlasTilesPerColumn = static_cast<int32_t>(kAtlasTilesPerColumn);
    tm->layer               = 0;
    tm->sortingKey          = 0;
    state.ecsEntities.push_back(ground);

    // Sprite row across the lower half (layer 2 -> over the tilemap).
    for (int i = 0; i < kSpriteCount; ++i) {
        ayt::entity::Entity* sp = world.createEntity();
        const std::string spriteName = std::string("sprite_") + std::to_string(i);
        sp->setName(spriteName.c_str());
        sp->addComponent<ayt::entity::Transform>();
        auto* sc = sp->addComponent<ayt::entity::SpriteComponent>();
        sc->setTexture(state.spritePath.c_str());
        sc->position = ayt::math::FVector3(-132.0f + i * 24.0f, -24.0f, 0.0f);
        sc->scaleX   = 18.0f;
        sc->scaleY   = 18.0f;
        sc->layer    = 2;
        sc->sortingKey = i;
        state.ecsEntities.push_back(sp);
    }

    // Primary ortho camera.
    ayt::entity::Entity* cam = world.createEntity();
    cam->setName("camera");
    cam->addComponent<ayt::entity::Transform>();
    auto* cc = cam->addComponent<ayt::entity::OrthoCameraComponent>();
    cc->zoom           = kCameraZoom;
    cc->viewSize       = kViewSize;
    cc->viewportAspect = static_cast<float>(kWindowWidth)
                       / static_cast<float>(kWindowHeight);
    cc->isPrimary      = true;
    state.ecsCamera     = cam;
    state.ecsCameraComp = cc;
    state.ecsEntities.push_back(cam);

    std::fprintf(stderr,
                 "[AY2DDemo] ECS line: %zu entities (ground + %d sprites + camera)\n",
                 state.ecsEntities.size(), kSpriteCount);
}

// ---- direct AY2D line ----------------------------------------------------------

void resetDirect2DPath(DemoState& state)
{
    if (state.direct.tmHandle.id != 0) {
        state.direct.world2d.removeTilemap(state.direct.tmHandle);
        state.direct.tmHandle = ayt::ay2d::TilemapHandle{};
    }
    state.direct.sprites.clear();
    state.direct.tilemap.reset();
    state.direct.ready = false;
    state.direct.gpuReady = false;
    std::fprintf(stderr, "[AY2DDemo] direct AY2D line reset\n");
}

void spawnDirect2DPath(DemoState& state)
{
    resetDirect2DPath(state);

    // World2D registry entry (layer/sortingKey come from the entry).
    state.direct.tmHandle = state.direct.world2d.addTilemap(0, 0);

    // Sprite row mirrors the ECS line (own Float3x3 world matrices).
    for (int i = 0; i < kSpriteCount; ++i) {
        ayt::ay2d::Sprite sp;
        sp.worldMatrix = makeAffine(-132.0f + i * 24.0f, -24.0f, 0.0f, 18.0f, 18.0f);
        sp.sourceRectU0 = 0.0f; sp.sourceRectV0 = 0.0f;
        sp.sourceRectU1 = 1.0f; sp.sourceRectV1 = 1.0f;
        sp.layer      = 2;
        sp.sortingKey = static_cast<uint32_t>(i);
        state.direct.sprites.push_back(sp);
    }

    // Real AY2D ortho camera (same framing as the ECS camera).
    ayt::ay2d::OrthographicCamera& cam = state.direct.camera;
    cam = ayt::ay2d::OrthographicCamera{};
    cam.viewport     = ayt::ay2d::ViewportRect{0, 0, kWindowWidth, kWindowHeight};
    cam.zoom         = kCameraZoom;
    cam.viewSize     = kViewSize;
    cam.layerMask    = 0xFFFFFFFFu;
    cam.atlasGutterIsZero = true;  // dense atlas, gutter 0

    // CPU-side resource: the .aytilemap (ResourceManager needs no
    // renderer). GPU assets are created lazily on the first builder
    // call (gpuReady below) — the renderer is not initialized until
    // the first loop tick.
    state.direct.tilemap = ayt::resource::ResourceManager::instance()
                               .load<ayt::resource::ITilemap>(state.tilemapPath);
    if (!state.direct.tilemap) {
        std::fprintf(stderr, "[AY2DDemo] load<ITilemap> failed: '%s'\n",
                     state.tilemapPath.c_str());
    }
    state.direct.ready = true;
    std::fprintf(stderr, "[AY2DDemo] direct AY2D line CPU-ready "
                 "(tilemap loaded=%d)\n", state.direct.tilemap ? 1 : 0);
}

// Lazily create the GPU assets for the direct line. Called from the
// scene builder on its first invocation (renderer is live by then).
void ensureDirectGPUAssets(DemoState& state)
{
    if (state.direct.gpuReady) {
        return;
    }
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        return;
    }
    ayt::render::Renderer& renderer = rss->renderer();
    state.direct.quad = renderer.createUnitQuad();
    state.direct.atlasTex  = renderer.loadTexture(state.atlasPath);
    state.direct.spriteTex = renderer.loadTexture(state.spritePath);
    state.direct.material  = renderer.createMaterialFromPhoskia(
        ayt::render::kTilemapPhoskiaSource, "ay2d_demo_direct");
    if (state.direct.material.isValid()) {
        renderer.setMaterialTexture(state.direct.material, "albedoMap",
                                    state.direct.atlasTex);
    }
    // Sprites sample their own texture (not the atlas) — a separate
    // material with albedoMap = spriteTex.
    state.direct.spriteMaterial = renderer.createMaterialFromPhoskia(
        ayt::render::kTilemapPhoskiaSource, "ay2d_demo_direct_sprite");
    if (state.direct.spriteMaterial.isValid()) {
        renderer.setMaterialTexture(state.direct.spriteMaterial, "albedoMap",
                                    state.direct.spriteTex);
    }
    state.direct.gpuReady = true;
    std::fprintf(stderr, "[AY2DDemo] direct line GPU assets ready "
                 "(material valid=%d, spriteMaterial valid=%d, atlas valid=%d)\n",
                 state.direct.material.isValid() ? 1 : 0,
                 state.direct.spriteMaterial.isValid() ? 1 : 0,
                 state.direct.atlasTex.isValid() ? 1 : 0);
}

// Scene builder registered once at startup; active only on the direct
// line. Builds tile DrawItems via the real ayt::ay2d::tileUV /
// cellToWorld and sprite DrawItems via buildSpriteScene ->
// SpriteDrawCmd translation, then sets the main camera (the builder
// chain runs after the default perspective camera setup, so this
// wins the frame — same contract as OrthoCameraUpdateSystem).
void buildDirectScene(DemoState& state, ayt::render::RenderScene& scene)
{
    if (state.path != DemoPath::Direct2D || !state.direct.ready) {
        return;
    }
    ensureDirectGPUAssets(state);  // lazy — renderer is live by first builder call
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        return;
    }
    ayt::render::Renderer& renderer = rss->renderer();
    if (!state.direct.quad.isValid() || !state.direct.material.isValid()) {
        return;
    }

    renderer.setMainCamera(state.direct.camera.viewMatrix(),
                           state.direct.camera.projectionMatrix());

    state.payloads.clear();

    struct Entry {
        ayt::render::DrawItem      item;
        ayt::render::DrawPayload2D payload;
    };
    std::vector<Entry> entries;

    // --- tile layer (from the loaded .aytilemap via AY2D math) ---
    if (state.direct.tilemap) {
        const ayt::resource::ITilemap& map = *state.direct.tilemap;
        const uint32_t cols = map.getCols();
        const uint32_t rows = map.getRows();
        if (cols > 0 && rows > 0) {
            ayt::ay2d::AtlasDesc desc{};
            desc.atlasWidthTexels  = kAtlasTexels;
            desc.atlasHeightTexels = kAtlasTexels;
            desc.tileWidthTexels   = kTilePx;
            desc.tileHeightTexels  = kTilePx;
            desc.tilesPerRow       = kAtlasTilesPerRow;
            desc.tilesPerColumn    = kAtlasTilesPerColumn;
            desc.gutter            = 0;  // dense atlas (palette grid)

            const float tileW = static_cast<float>(map.getTileWidth());
            const float tileH = static_cast<float>(map.getTileHeight());
            const uint32_t sortKey =
                ayt::ay2d::World2D::packSortKey(0, 0);  // tilemap layer

            for (uint32_t row = 0; row < rows; ++row) {
                for (uint32_t col = 0; col < cols; ++col) {
                    const uint32_t tileId = tileIdAt(map, col, row);
                    const ayt::ay2d::TileUV uv =
                        ayt::ay2d::tileUV(tileId, desc);
                    const ayt::math::FVector2 center =
                        ayt::ay2d::cellToWorld(
                            ayt::ay2d::TileCoord{static_cast<int32_t>(col),
                                                 static_cast<int32_t>(row)},
                            ayt::math::FVector2(0.0f, 0.0f), tileW, tileH);

                    Entry e;
                    e.payload.sourceRectMin = ayt::math::FVector2(uv.uMin, uv.vMin);
                    e.payload.sourceRectMax = ayt::math::FVector2(uv.uMax, uv.vMax);
                    e.payload.tintRGBA      = ayt::math::FVector4(1, 1, 1, 1);
                    e.payload.flip          = 0;
                    e.payload.packedSortKey = sortKey;
                    e.item.mesh     = state.direct.quad;
                    e.item.material = state.direct.material;
                    e.item.world    = ayt::math::Transform::getMatrix(
                        ayt::math::FVector3(center.x, center.y, 0.0f),
                        ayt::math::FQuaternion::identity(),
                        ayt::math::FVector3(tileW, tileH, 1.0f));
                    entries.push_back(e);
                }
            }
        }
    }

    // --- sprite layer via buildSpriteScene (AABB cull + stable_sort) ---
    std::vector<ayt::ay2d::SpriteDrawCmd> cmds;
    ayt::ay2d::buildSpriteScene(state.direct.sprites, state.direct.camera, cmds);
    for (const ayt::ay2d::SpriteDrawCmd& cmd : cmds) {
        Entry e;
        e.payload.sourceRectMin = cmd.sourceRectMin;
        e.payload.sourceRectMax = cmd.sourceRectMax;
        e.payload.tintRGBA      = cmd.colorRGBA;
        e.payload.flip          = cmd.flip;
        e.payload.packedSortKey = cmd.packedSortKey;
        e.item.mesh     = state.direct.quad;
        e.item.material = state.direct.spriteMaterial;
        e.item.world    = to4x4(cmd.worldMatrix);
        entries.push_back(e);
    }

    state.payloads.reserve(entries.size());
    for (Entry& e : entries) {
        state.payloads.push_back(e.payload);
        e.item.payload = &state.payloads.back();
        scene.add(e.item);
    }

    static int s_logCount = 0;
    if ((++s_logCount % 60) == 1) {
        std::fprintf(stderr, "[AY2DDemo] direct line: %zu items this frame\n",
                     entries.size());
    }
}

// ---- Win32 --------------------------------------------------------------------

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DemoState* state = reinterpret_cast<DemoState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CLOSE:
        if (state != nullptr) {
            state->running = false;
            if (state->loop != nullptr) {
                state->loop->stop();
            }
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEWHEEL: {
        // Wheel = zoom about the screen center. 120 units per notch.
        if (state != nullptr) {
            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const float factor = std::pow(1.1f, static_cast<float>(delta) / 120.0f);
            state->zoom = std::clamp(state->zoom * factor, kMinZoom, kMaxZoom);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        // Left-drag = manual pan; snap the drag baseline to the live
        // camera so there is no jump on press.
        if (state != nullptr) {
            state->dragging = true;
            state->autoPan = false;  // first drag takes over the orbit
            state->dragLastCursor.x = GET_X_LPARAM(lParam);
            state->dragLastCursor.y = GET_Y_LPARAM(lParam);
            if (state->path == DemoPath::Ecs && state->ecsCameraComp != nullptr) {
                state->dragCamX = state->ecsCameraComp->positionX;
                state->dragCamY = state->ecsCameraComp->positionY;
            } else {
                state->dragCamX = state->direct.camera.positionX;
                state->dragCamY = state->direct.camera.positionY;
            }
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        // Incremental drag: worldDelta = pixelDelta * (viewHeight/zoom / winHeight).
        if (state != nullptr && state->dragging) {
            POINT cur{};
            cur.x = GET_X_LPARAM(lParam);
            cur.y = GET_Y_LPARAM(lParam);
            const float pxToWorld = kViewSize / static_cast<float>(kWindowHeight) / state->zoom;
            // +y world is up: dragging right moves content right => cam left;
            // dragging down moves content down => cam up (+y).
            state->dragCamX -= static_cast<float>(cur.x - state->dragLastCursor.x) * pxToWorld;
            state->dragCamY += static_cast<float>(cur.y - state->dragLastCursor.y) * pxToWorld;
            state->dragLastCursor = cur;
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (state != nullptr) {
            state->dragging = false;
        }
        ReleaseCapture();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND createDemoWindow(DemoState* state)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = windowProc;
    wc.hInstance     = instance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AY2DEngineDemo";
    RegisterClassExW(&wc);

    RECT rect{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"AY2D Engine Demo — ECS | direct AY2D",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, nullptr);

    if (hwnd != nullptr) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

void pumpWin32Messages(DemoState& state)
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            state.running = false;
            if (state.loop != nullptr) {
                state.loop->stop();
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Camera orbit around the map center — shared by both lines.
void updateCameraPan(float elapsed, float& x, float& y)
{
    x = kMapCenterX + 44.0f * std::cos(elapsed * 0.35f);
    y = kMapCenterY + 30.0f * std::sin(elapsed * 0.45f);
}

void switchPath(DemoState& state, DemoPath next)
{
    if (state.path == next) {
        return;
    }
    state.autoPan = true;    // path switch restarts the camera orbit
    state.dragging = false;
    if (next == DemoPath::Ecs) {
        resetDirect2DPath(state);
        spawnEcsScene(state);
    } else {
        clearEcsScene(state);
        spawnDirect2DPath(state);
    }
    state.path = next;
    std::fprintf(stderr, "[AY2DDemo] switched to path %d (%s)\n",
                 static_cast<int>(next),
                 next == DemoPath::Ecs ? "ECS" : "direct AY2D");
}

} // namespace

int main()
{
    DemoState state;
    ayt::game::GameLoop& loop = ayt::game::GameLoop::instance();
    state.loop = &loop;

    HWND hwnd = createDemoWindow(&state);
    if (hwnd == nullptr) {
        std::fprintf(stderr, "[AY2DDemo] failed to create Win32 window\n");
        return 1;
    }

    ayt::render::RendererSubSystem::setBootstrapWindow(
        hwnd, static_cast<uint32_t>(kWindowWidth),
        static_cast<uint32_t>(kWindowHeight));

    char tempDir[MAX_PATH] = {};
    std::string assetRootPrefix = "ay2d_engine_demo\\";
    if (GetTempPathA(MAX_PATH, tempDir) > 0) {
        assetRootPrefix = std::string(tempDir) + "ay2d_engine_demo\\";
    }
    if (!bakeDemoAssets(assetRootPrefix, state)) {
        std::fprintf(stderr, "[AY2DDemo] bakeDemoAssets failed\n");
        return 1;
    }
    std::fprintf(stderr, "[AY2DDemo] shaderc hint: %s (exists=%d)\n",
                 AY_SHADER_SHADERC_HINT,
                 fileExists(AY_SHADER_SHADERC_HINT) ? 1 : 0);
    std::fprintf(stderr, "[AY2DDemo] Esc quit | 1/2 switch path | "
                         "wheel zoom | left-drag pan (disables orbit) | "
                         "shots at 30/60 | AY2D_DEMO_FRAMES=n auto-exit\n");

    loop.setTargetFPS(60.0f);
    loop.setRenderThreadEnabled(false);

    // Engine integration order (mirror EngineIntegrationDemo):
    // bootstrapModule() wires the 刀 2 systems (their onStart runs on
    // the first world update — after RendererSubSystem registered).
    ayt::entity::bootstrapModule();
    ayt::render::RendererSubSystem::registerSubSystem();

    // Direct-line scene builder (inert while path == ECS).
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss != nullptr) {
        rss->setSceneBuilder([&state](ayt::render::RenderScene& scene) {
            buildDirectScene(state, scene);
        });
    }

    // Headless acceptance: AY2D_DEMO_PATH=2 starts directly on the
    // direct-AY2D line (key switching still works at runtime).
    char envPath[4] = {};
    if (GetEnvironmentVariableA("AY2D_DEMO_PATH", envPath,
                                static_cast<DWORD>(sizeof(envPath))) > 0
        && std::string(envPath) == "2") {
        switchPath(state, DemoPath::Direct2D);
    } else {
        spawnEcsScene(state);
    }

    // AY2D_DEMO_FRAMES=<n> restores the old auto-exit cap for
    // headless acceptance runs; default 0 = run until Esc / close.
    char envFrames[16] = {};
    if (GetEnvironmentVariableA("AY2D_DEMO_FRAMES", envFrames,
                                static_cast<DWORD>(sizeof(envFrames))) > 0) {
        state.frameCap = std::atoi(envFrames);
    }

    const auto startTime = std::chrono::steady_clock::now();

    const uint64_t listenerId = loop.onUpdate([&](float /*deltaTime*/) {
        pumpWin32Messages(state);
        if (!state.running) {
            loop.stop();
            return;
        }

        // Key-driven path switch (edge detected via latches).
        const bool k1 = (GetAsyncKeyState('1') & 0x8000) != 0;
        const bool k2 = (GetAsyncKeyState('2') & 0x8000) != 0;
        if (k1 && !state.key1Down) {
            switchPath(state, DemoPath::Ecs);
        }
        if (k2 && !state.key2Down) {
            switchPath(state, DemoPath::Direct2D);
        }
        state.key1Down = k1;
        state.key2Down = k2;

        // Interactive camera: wheel zoom + left-drag pan. A drag takes
        // over the position for that frame; autoPan turns off permanently
        // on the first drag so the manual framing sticks after release.
        const auto now      = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - startTime).count();
        float camX = 0.0f, camY = 0.0f;
        if (state.dragging && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            camX = state.dragCamX;  // manual pan this frame
            camY = state.dragCamY;
        } else {
            if (state.dragging) {
                state.dragging = false;  // button lost (e.g. path switch)
            }
            if (state.autoPan) {
                updateCameraPan(elapsed, camX, camY);
            } else {
                camX = state.dragCamX;  // manual mode: freeze at last drag pos
                camY = state.dragCamY;
            }
        }

        if (state.path == DemoPath::Ecs) {
            if (state.ecsCameraComp != nullptr) {
                state.ecsCameraComp->positionX = camX;
                state.ecsCameraComp->positionY = camY;
                state.ecsCameraComp->zoom      = state.zoom;
            }
            // Gentle sprite animation on the ECS line.
            for (size_t i = 0; i < state.ecsEntities.size(); ++i) {
                auto* sc = state.ecsEntities[i]->getComponent<ayt::entity::SpriteComponent>();
                if (sc == nullptr) {
                    continue;
                }
                if (i % 3 == 0) {
                    sc->rotationZ = std::sin(elapsed * 1.2f + static_cast<float>(i)) * 0.3f;
                } else if (i % 3 == 1) {
                    sc->scaleX = 18.0f + 6.0f * std::sin(elapsed * 0.9f + static_cast<float>(i));
                }
            }
        } else {
            state.direct.camera.positionX = camX;
            state.direct.camera.positionY = camY;
            state.direct.camera.zoom      = state.zoom;
            // Direct-line sprite animation (rebuild affine matrices).
            for (int i = 0; i < kSpriteCount; ++i) {
                float rot = 0.0f;
                float sx = 18.0f, sy = 18.0f;
                if (i % 3 == 0) {
                    rot = std::sin(elapsed * 1.2f + static_cast<float>(i)) * 0.3f;
                } else if (i % 3 == 1) {
                    sx = 18.0f + 6.0f * std::sin(elapsed * 0.9f + static_cast<float>(i));
                }
                state.direct.sprites[i].worldMatrix =
                    makeAffine(-132.0f + i * 24.0f, -24.0f, rot, sx, sy);
            }
        }

        // Screenshots at the configured moments.
        for (int i = 0; i < kShotCount; ++i) {
            if (state.frame == kShotFrames[i]) {
                char name[64];
                std::snprintf(name, sizeof(name), "frame_%02d", state.frame);
                const std::string base = state.screenshotDir + name;
                ayt::render::RendererSubSystem* shotRss =
                    ayt::render::RendererSubSystem::findRegistered();
                if (shotRss != nullptr && shotRss->renderer().captureScreenshot(base)) {
                    std::fprintf(stderr,
                                 "[AY2DDemo] frame %d -> screenshot %s (.tga/.png)\n",
                                 state.frame, base.c_str());
                } else {
                    std::fprintf(stderr,
                                 "[AY2DDemo] frame %d captureScreenshot failed\n",
                                 state.frame);
                }
            }
        }

        // Headless acceptance cap (AY2D_DEMO_FRAMES); 0 = unlimited.
        if (state.frameCap > 0 && state.frame >= state.frameCap) {
            std::fprintf(stderr, "[AY2DDemo] reached %d frames; stopping loop\n",
                         state.frameCap);
            state.running = false;
            loop.stop();
            return;
        }
        state.frame++;

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            state.running = false;
            loop.stop();
        }
    });

    loop.run();
    loop.offUpdate(listenerId);
    loop.shutdown();

    std::fprintf(stderr, "[AY2DDemo] done. frames=%d\n", state.frame);
    std::printf("AY2DEngine_Demo finished.\n");
    return 0;
}
