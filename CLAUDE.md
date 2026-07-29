# AY2D 项目 AI 工作注意事项

> **注意**：AY2D 是独立子模块，遵循本文件定义的规则。AYTest 是独立测试框架库，位于 `AYTest/CLAUDE.md`。
> **权威设计**：[`design.md`](design.md)（v0.1.9, 2026-07-29，含工业级审核 patch F-1..F-19 + Changelog §13.1..§13.11 + Phase 3C counter wiring §14 + Phase 3D batch tile-fill §15 + Phase 3E world↔cell math §16 + Phase 3F sprite culling §17）。代码与 design.md 不一致时，design.md 优先。

## 当前状态 — Phase 3F in-AY2D sprite culling helper (2026-07-29)

- `design.md` 是权威设计（v0.1.9 + audit F-1..F-19 + Changelog §13.1..§13.11 + §14 P3C + §15 P3D + §16 P3E + §17 P3F）。代码与 design.md 不一致时，design.md 优先。
- 子模块 HEAD = Phase 3F in-AY2D sprite scene builder：`SpriteDrawCmd` POD（约 88B per cmd，纯 AYSprite + AYMath，**不**触 AYRenderer）+ `WorldAabb(camera)` 内联 helper 推导相机 world AABB + 自由函数 `buildSpriteScene(sprites, camera, out)` 在 `src/AYSpriteCulling.cpp` 走 AABB 预剔除 → layer-mask 剔除 → `std::stable_sort` by `packedSortKey` → 输出 cmd。
- `unittest/` 现在有 **16** 个 test 文件 / **459** CHECK assertions PASS（Phase 3E 15/114/431 → Phase 3F 16/??/459 = +1 suite, +10 case, +28 CHECK）。新增 `Test_SpriteCulling` 走真 wired delta（同 P3C/P3D/P3E 纪律）。
- 锁行为（§17.2 / R-3F.1..7）：NO AYRenderer include（R-3F.1 验证）· 退化相机（`viewSize<=0` 或 `viewport.{w,h}Px<=0`）cull-level short-circuit 直接 empty output，**不**依赖 `FRectangle::intersects` 对空 rect 的严格 less-compare 语义（R-3F.2 踩坑 — 见 ay-2d.md 踩坑 #24）· sprite AABB = translation ±0.5（R-3F.4），不考虑 scale/rotation（matrix-aware bounds 留给 cross-module PR）· layer-mask bit test 前置剔除再排序（R-3F.5）· `std::stable_sort` 不是 `std::sort`（R-3F.6 / F-2，test case 7 用 `m[6]` 读 X translation 锁顺序）· 纯数据 carrier，**不**做 CPU 投影（R-3F.7，per-frame matrix projection 是 GPU-side via AYRenderer）。
- P3F 给未来的 cross-module PR（§4.2.1）准备好了 `SpriteDrawCmd → DrawItem::payload` 翻译面，但不写翻译（必须由 AYRenderer maintainer 拥有 merge gate）。
- `World2D`/`Tilemap`/`OrthographicCamera` 现有 surface 不变；P3F 是叠加 API。
- **R-10 lock 守住**：AY2D 没有 AYAnimation include / link；动画表是 AY2D 自管 + AYTime-free。
- `add_library(AY2D STATIC ${SRC_FILES})` 持续生效；`target_link_libraries(AY2D PUBLIC AYMath)` 持续生效。
- `cmake/CheckNoBgfxInPublicHeaders.cmake` 是 bgfx-leak guard (§11.2 / F-5)；双向验证已通过（17 public headers scanned, 0 leaks；新 headers `AYSpriteDrawCmd.h` / `AYWorldAabb.h` / `AYSpriteCulling.h` 都只 include stdint + aymath/MathTypes.h + in-AY2D）。
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
