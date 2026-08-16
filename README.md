# AY2D

AY2D 是 AY Engine 的 2D 世界与呈现模块，提供 Tilemap、Sprite、正交相机、图集、瓦片动画、碰撞查询和分块流送能力。

## 公开接口

```cpp
#include <AY2D.h>
#include <AY2D/Sprite.h>
#include <AY2D/Tilemap.h>
#include <AY2D/OrthographicCamera.h>
```

入口头文件 `AY2D.h` 位于模块根目录；公开实现头位于 `include/AY2D/`。

## 依赖

- AYMath
- AYLog
- AYTest（仅测试）

## 构建

```powershell
cmake --build <build-dir> --target AY2D
ctest --test-dir <build-dir> -R AY2D
```

完整架构和阶段状态见 [design.md](design.md)。
