# AY2D 项目 AI 工作注意事项

> **注意**：AY2D 是独立子模块，遵循本文件定义的规则。AYTest 是独立测试框架库，位于 `AYTest/CLAUDE.md`。
> **权威设计**：[`design.md`](design.md)（v0.1.7, 2026-07-29，含工业级审核 patch F-1..F-19 + Changelog §13.1..§13.9 + Phase 3C counter wiring §14 + Phase 3D batch tile-fill §15）。代码与 design.md 不一致时，design.md 优先。

## 当前状态 — Phase 3D in-AY2D batch tile-fill API (2026-07-29)

- `design.md` 是权威设计（v0.1.7 + audit F-1..F-19 + Changelog §13.1..§13.9 + §14 P3C + §15 P3D）。代码与 design.md 不一致时，design.md 优先。
- 子模块 HEAD = Phase 3D in-AY2D batch tile-fill API：`setTileRange` / `fillTile` / `copyTileRange` / `gridRect` 四个新 free function + 新公共类型 `TileRect`（half-open `[x0, x1)` x `[y0, y1)` POD 16B），全部实现在 `src/AYTilemapBatch.cpp`。
- `unittest/` 现在有 **14** 个 test 文件 / **106** TEST_CASE / **396** CHECK assertions PASS（Phase 3C 13/96/331 → Phase 3D 14/106/396 = +1 suite, +10 case, +65 CHECK）。新增 `Test_TilemapBatch` 走真 wired delta（同 P3C 纪律）。
- 锁行为：half-open rect · silent clamp · empty rect no-op · `copyTileRange` width-mismatch no-op（F-18）· 「one batch = one mutation」（P3C no-double-counting 继承）· batch lazy-fill 用 `defaultTileId`（不漏为 `tileId`，R-3D.4）。
- `Tilemap` 现有 `setTile` / `getTile` / `resizeGrid` / `loadChunkFromSource` / tick 接口签名保持不变；batch 是叠加 API，不破坏 P2/P3A/P3B 任何测试。
- `World2D::addTilemap` / `removeTilemap` / `swapTilemap` + `InMemoryTilemapChunkSource` + chunk-source mutation 路径保留 P3C 真 wired 行为。
- **R-10 lock 守住**：AY2D 没有 AYAnimation include / link；动画表是 AY2D 自管 + AYTime-free。
- `add_library(AY2D STATIC ${SRC_FILES})` 持续生效；`AYMath` 是 Phase 3A 起 PUBLIC link 依赖。
- `cmake/CheckNoBgfxInPublicHeaders.cmake` 是 bgfx-leak guard (§11.2 / F-5)；双向验证已通过（新 header `AYTileRect.h` 只 include `<cstdint>`）。
- 跨模块 PR 仍按 `design.md` §4.2.1 deferred：AYRenderer / AYResource / AYEntity / AYShader maintainer 拥有各自的 merge gate。

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
