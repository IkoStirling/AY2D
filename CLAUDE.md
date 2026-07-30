# AY2D 项目 AI 工作注意事项

> **注意**：AY2D 是独立子模块，遵循本文件定义的规则。AYTest 是独立测试框架库，位于 `AYTest/CLAUDE.md`。
> **权威设计**：[`design.md`](design.md)（v0.1.12, 2026-07-30，含工业级审核 patch F-1..F-19 + Changelog §13.1..§13.14 + §13.PF pre-flight retractions + Phase 3C counter wiring §14 + Phase 3D batch tile-fill §15 + Phase 3E world↔cell math §16 + Phase 3F sprite culling §17 + Phase 3G chunk-source budget §18）。代码与 design.md 不一致时，design.md 优先。

## 当前状态 — P3H.2 in-AY2D World2DSnapshot value type (2026-07-30)

- `design.md` 是权威设计（v0.1.12 + audit F-1..F-19 + Changelog §13.1..§13.14 + §13.PF + §14 P3C + §15 P3D + §16 P3E + §17 P3F + §18 P3G）。代码与 design.md 不一致时，design.md 优先。
- 子模块 HEAD = P3H.2：`TilemapEntryView` POD（handle + layer + sortingKey，无 resource field）+ `World2DSnapshot` value type + `World2DSnapshot::build(const World2D&)`。`TilemapBinding` 标记 `[[deprecated]]`（§13.PF C5 reshape —— 用 value type 替代原计划的 `IWorld2DDebug` vtable，保持 `World2D` POD-ish）。
- `unittest/` 现在有 **19** 个 test 文件 / **559** CHECK assertions PASS（Phase 5 18/538 → P3H.2 19/559 = +1 suite, +21 CHECK）。新增 `Test_World2DSnapshot` 走真 wired delta（5 case：empty / size matches world / counters stable / entry view fields / TilemapBinding backcompat）。
- 锁行为（§18.6 / R-3G.1..7 + §13.PF）：**R-3G.1** non-LRU policy = no-op + false（active lock；Distance/TimeWindow 留给 Phase 4 streaming 跨模块 PR，§13.PF clarification）；**R-3G.4** `maxChunksResident` 是 GPU residency 撞 §4.2.1（cross-module PR to RenderResourceManager，Phase 6）；§13.PF C5 `TilemapBinding` deprecated；§13.PF C6 `flagsAtRaw` body returns `Empty` (1<<6) not `None` (0)（Phase 5 修复 + 测试覆盖）；§13.PF C8 `TileCoord` deviation shipped interface 用 `TileCoord` 而非 `IVector2`；§11.2 Phase 5 row exit gate = "Interface compiles; unit tests for cell lookup"（已 ship）。
- 不破坏 P3A / P3B / P3C / P3D / P3E / P3F / P3G / Phase 5 任何已有测试。
- **R-10 lock 守住**：AY2D 没有 AYAnimation include / link。
- `add_library(AY2D STATIC ${SRC_FILES})` 持续生效；`target_link_libraries(AY2D PUBLIC AYMath)` 持续生效。
- `cmake/CheckNoBgfxInPublicHeaders.cmake` 是 bgfx-leak guard (§11.2 / F-5)；双向验证已通过（20 public headers scanned, 0 leaks；新 `AYWorld2DSnapshot.h` 是 `<cstdint>` + `<vector>` + `AY2DCounters.h` + `AYWorld2D.h`，纯 std+existing）。
- 跨模块 PR 仍按 `design.md` §4.2.1 deferred：AYRenderer / AYResource / AYEntity / AYShader / AYPhysics maintainer 拥有各自的 merge gate。P3H.2 不开任何跨模块 PR（vtable-free 设计）。

## 重要规则

1. **UTF-8 only，禁止 GBK 中文注释** — 与其它 sibling 模块一致；本仓库 `design.md` 中文为合法文档内容，但 `.h / .cpp / .cmake` 不应有 GBK。
2. **代码与 design.md 不一致时，design.md 优先** — 修改设计须先改 design.md §13 Changelog，再动代码。
3. **仍然 Phase 1+ 禁止**：
   - 写 `bgfx::*` 调用；
   - 触碰其它模块源码（详见 `design.md` §4.2.1 cross-module PR ownership）；
   - **修改根 `AY_ENABLE_AY2D` 的默认值**——`OFF` 是用户最终决策，不容擅自改动。
4. **公共头零 bgfx** — 所有 `include/AY2D/**/*.h` 不得 include `<bgfx/*.h>` / `<bx/*.h>` / `AYRenderer/src/detail/*`。Phase 1 CI 通过 `ay2d_check_no_bgfx_in_public_headers` target 强制（F-5）。
5. **可主动构建 + 跑测试** — 已 OK。`cmake -B build_test -G Ninja -DAY_ENABLE_AY2D=ON -DCMAKE_TOOLCHAIN_FILE=D:/Projects/vcpkg/scripts/buildsystems/vcpkg.cmake` 跑通；`ninja ay2d_check_no_bgfx_in_public_headers` 双向往返验证。

## 命名约定（与 sibling 一致）

- 子模块 / 目录：`AYRuntime/AY2D`（无前缀拼写例外）。
- 命名空间：`ayt::ay2d`（与 `ayt::physics` / `ayt::entity` / `ayt::render` 同级）。
- 公共类名**不带** `AY` 前缀（`World2D` / `Tilemap` / `Sprite` / `OrthographicCamera` / `ITileCollisionQuery` / `ITilemapChunkSource`）。
- 公共头文件名带 `AY` 前缀（`AYWorld2D.h` / `AYTilemap.h` / `AYSprite.h` / `AYOrthographicCamera.h` / `AYTileCollision.h` / `AYTilemapChunkSource.h`）。
- 接口类型 `I` 前缀。
- 私有成员 `_` 前缀 camelCase；公共成员 plain camelCase。
- 测试：`unittest/Test_<Subject>.cpp` 一文件一 TU；用 AYTest 的 `TEST_SUITE` / `TEST_CASE` / `CHECK_*`。

## 反模式（与 `design.md` §4 / §11.2 同步）

| 反模式 | 替代 |
|---|---|
| 在 `include/AY2D/*.h` 引入 `<bgfx/bgfx.h>` | 公共头零 bgfx；GPU 句柄仅在 `AYRenderer/src/detail/*` 内可见 |
| 直接 `bgfx::submit` / `bgfx::createTexture2D` | 通过 `Renderer` / `RenderResourceManager` 公共 API |
| `World2D` 持 `bgfx::TextureHandle` | 持 path 或 `MaterialHandle` |
| 修改 `DrawItem` 既有的 3D 字段 | 走 `DrawItem::payload` 模式（`design.md` §7.1；Phase 2+ 跨模块 PR） |
| 引入新的 `.ay*` 文件类型 | 复用 `.aymesh` extension four-cc chunk 策略（`design.md` §9.2） |
| 在 ECS 组件内直接 `bgfx::*` | 组件只持 path / opaque handle / world matrix |
| `std::sort` 用于 sortKey 排序 | `std::stable_sort`（`design.md` §7.4 / F-2） |
| 静态全局 `g_ay2d_*` counter | 实例字段 `std::atomic<uint64_t>`（`design.md` §10.1.1） |
| 引入 `AYUI` 依赖（capability-map L85） | Editor glue 在 `AYEditor`，不在 `AY2D` |
| 跨模块 PR self-merge | cross-module PR 由对应模块 maintainer 合并（`design.md` §4.2.1） |

## 引用

- [design.md](design.md) — 权威设计（v0.1 + F-1..F-19 audit）。
- 兄弟模块设计：`d:/Projects/AYRuntime/AYRenderer/design.md`、`AYEntity/design.md`、`AYResource/design.md`、`AYPhysics/design.md`、`AYShader/design.md`。
- 根目录 docs：`d:/Projects/ENGINE-FOUNDATION-PLAN.md`、`ENGINE-DETERMINISM-ARCHITECTURE.md`、`AYRendering-Architecture-Roadmap.md`、`AYRuntime/docs/first-game-engine-capability-map.md §E L72-85`。
