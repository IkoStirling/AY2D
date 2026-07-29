# AY2D 项目 AI 工作注意事项

> **注意**：AY2D 是独立子模块，遵循本文件定义的规则。AYTest 是独立测试框架库，位于 `AYTest/CLAUDE.md`。
> **权威设计**：[`design.md`](design.md)（v0.1.4, 2026-07-29，含工业级审核 patch F-1..F-19 + Changelog §13.1..§13.6）。代码与 design.md 不一致时，design.md 优先。

## 当前状态 — Phase 3 in-AY2D real impl promotions (2026-07-29)

- `design.md` 是权威设计（v0.1.4 + audit F-1..F-19 + Changelog §13.1..§13.6）。代码与 design.md 不一致时，design.md 优先。
- 子模块已被 root 仓库注册：`option(AY_ENABLE_AY2D ... OFF)` + conditional `add_subdirectory(AYRuntime/AY2D)` (默认 OFF)。
- 子模块 HEAD = Phase 3 in-AY2D real impl：3 个 .cpp（`AYTilemap.cpp` + `AYInMemoryTilemapChunkSource.cpp` + `AYWorld2D.cpp`） + 11 个公共头（`AY2DCounters.h` 是新的） + ChunkRequestHandle 升级成 24+8 packed id with generation。
- `unittest/` 现在有 10 个 test 文件，链接 AYTest + AYMath 跑通 10 个 TEST_SUITE / 60+ TEST_CASE。
- `add_library(AY2D STATIC ${SRC_FILES})` 持续生效；`AYMath` 是新增的 PUBLIC link 依赖（仅 OrthographicCamera 的 Float4x4 矩阵）。
- `cmake/CheckNoBgfxInPublicHeaders.cmake` 是 bgfx-leak guard (§11.2 / F-5)；双向验证已通过。
- 跨模块 PR 仍按 `design.md` §4.2.1 deferred：AYRenderer / AYResource / AYEntity maintainer 拥有各自的 merge gate。

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
