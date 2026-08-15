# AY2D 项目 AI 工作注意事项

> **注意**：AY2D 是独立子模块，遵循本文件定义的规则。AYTest 是独立测试框架库，位于 `AYTest/CLAUDE.md`。
> **权威设计**：[`design.md`](design.md)（v0.1.21, 2026-07-30，含工业级审核 patch F-1..F-19 + Changelog §13.1..§13.23 + §13.PF pre-flight retractions + Phase 3C counter wiring §14 + Phase 3D batch tile-fill §15 + Phase 3E world↔cell math §16 + Phase 3F sprite culling §17 + Phase 3G chunk-source budget §18 + §18.7 P3I.2 model + §13.22 P3I.3 L-7 coverage + §13.23 P3I.4 snapshot diff）。代码与 design.md 不一致时，design.md 优先。

## 当前状态 — D3 in-AY2D `TilemapCollisionQueryAdapter::raycast` Amanatides-Woo DDA walker (A-7, 2026-07-31, **Phase 3K ship**)

- `design.md` 是权威设计（v0.1.31 + audit F-1..F-19 + Changelog §13.1..§13.35 + §13.PF + §14 P3C + §15 P3D + §16 P3E + §17 P3F + §18 P3G + §18.7 P3I.2 + §13.22 P3I.3 + §13.23 P3I.4 + §13.24..§13.33 P3J + §13.34 P3J-hygiene + §13.35 D3 + §8.1.1 walker spec）。代码与 design.md 不一致时，design.md 优先。
- 子模块 HEAD = D3：`TilemapCollisionQueryAdapter::raycast` 实现 §13.13 placeholder → §13.35 真实 Amanatides-Woo 2D DDA walker（~110 行 body + 3 新 include `<cmath>` / `<limits>` / `AY2D/TileMath.h`）。Walker 是 geometry-only；resolver consumer 跨模块 (§4.2.1)。
- `unittest/` 现在有 **34** 个 test 文件 / **1414** CHECK assertions PASS（P3J 34/1372 → D3 34/1414 = +42 CHECK, +0 suite；9 个 D3 case + 1 reshape = 10 cases / 42 CHECK delta in `TileCollisionQuerySuite` 7→16 cases / 22→64 CHECK；3× consecutive **incremental** green locked + cold-configure green after D3 feat）。bgfx-leak guard green。
- 锁行为（L-3D-1..L-3D-6 全部生效；L-3J-1..L-3J-10 持续生效；L-3I-1..L-3I-13 持续生效；§3.4 epoch / §13.14 view shape / §13.19 view shape / §18.7 one-source-per-tilemap / L-3 / L-4 / R-10 全部守住）。
- **R-10 lock 守住**：AY2D 没有 AYAnimation include / link。
- `add_library(AY2D STATIC ${SRC_FILES})` 持续生效；`target_link_libraries(AY2D PUBLIC AYMath AYLog)` 持续生效。
- `cmake/CheckNoBgfxInPublicHeaders.cmake` 是 bgfx-leak guard (§11.2 / F-5)；双向验证已通过（21 public headers scanned, 0 leaks；D3 公共头增量仅 `AY2D/TilemapCollisionAdapter.h` raycast docblock，无 bgfx 路径；D3 .cpp walker body 0 bgfx/bx 路径 per G2 gate）。
- **Phase 3K 完整 ship** (1/1 slice + J-env root ops still active: D3 L-3D-1..6 six new locks / L-3D-1 walker early-exit predicate `flagsAtRaw(c) != Empty` / L-3D-2 `hit.t` along ORIGINAL direction (pointAt invariant) / L-3D-3 degenerate direction → no-hit sentinel / L-3D-4 OOB origin → snap / L-3D-5 ray.tMin skip leading cells / L-3D-6 maxDistance hard cutoff inclusive)。跨模块 PR (CM-1..CM-5) 仍按 `design.md` §4.2.1 deferred。**Phase 3L / 下一波 in-AY2D residue = 无**（Phase 3I + 3J + 3K 已清空所有 listed residue）。§8.1 doc-vs-code drift (4-param form vs shipped 2-param) logged in §13.35 — NOT fixed here, separate hygiene commit.

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
- 公共头文件名带 `AY` 前缀（`AY2D/World2D.h` / `AY2D/Tilemap.h` / `AY2D/Sprite.h` / `AY2D/OrthographicCamera.h` / `AY2D/TileCollision.h` / `AY2D/TilemapChunkSource.h`）。
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
